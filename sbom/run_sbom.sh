#!/usr/bin/env bash
# Worked contrast on a SAMPLE manifest (sbom/vuln-sample/), not the device. The lab's SBOM step is
# ./sbom/scan-running.sh (the firmware image your twin is running + the HMI container).
# Generates two SBOM formats and scans for CVEs. Narrates each step.
# Run from anywhere:  bash sbom/run_sbom.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"            # sbom/
source "$HERE/../tools/teach.sh" 2>/dev/null || true
declare -F step    >/dev/null 2>&1 || step()    { printf '\n==== %s ====\n' "$*"; }
declare -F say     >/dev/null 2>&1 || say()     { printf '>  %s\n' "$*"; }
declare -F run     >/dev/null 2>&1 || run()     { printf '   $ %s\n' "$*"; "$@"; }
declare -F observe >/dev/null 2>&1 || observe() { printf '-> %s\n' "$*"; }
declare -F predict >/dev/null 2>&1 || predict() { printf '?  PREDICT FIRST: %s\n' "$*"; }
declare -F finding >/dev/null 2>&1 || finding() { printf 'FINDING: %s\n' "$*"; }
declare -F note    >/dev/null 2>&1 || note()    { printf '   %s\n' "$*"; }
cd "$HERE"
mkdir -p out

# grype's first run downloads/loads a large vulnerability DB (hundreds of MB to a
# few GB) and can take several minutes on a fresh Codespace — with no output, it
# looks frozen. Run it with a live elapsed-time timer so students see it is alive,
# then reuse the result. Warm runs take ~30s.
grype_progress() {            # usage: grype_progress <outfile> <show:yes|no> <grype args...>
  local out="$1" show="$2"; shift 2
  if [ ! -f "$HERE/out/.grype_warm" ]; then
    say "First scan only: grype now loads its vulnerability database. On a fresh Codespace this can take 1-5 minutes (it may need to download it); later scans take ~30s. The timer below proves it is working, not stuck — please wait."
  fi
  local tmp; tmp="$(mktemp)"; local err; err="$(mktemp)"
  grype "$@" >"$tmp" 2>"$err" &
  local pid=$! t0=$SECONDS
  while kill -0 "$pid" 2>/dev/null; do
    printf '\r   grype working... %3ss elapsed (first run loads/downloads the CVE database)   ' "$((SECONDS - t0))"
    sleep 1
  done
  wait "$pid"; local rc=$?
  printf '\r   grype finished in %ss.                                                      \n' "$((SECONDS - t0))"
  cp "$tmp" "$out" 2>/dev/null || true
  [ "$show" = yes ] && cat "$tmp"
  [ -s "$err" ] && grep -viE 'no explicit name|the default behavior' "$err" >&2 || true
  [ "$rc" -eq 0 ] && touch "$HERE/out/.grype_warm"
  rm -f "$tmp" "$err"
  return "$rc"
}

step "SBOM of a SAMPLE manifest ( sbom/vuln-sample/ ) — a worked contrast, not your device (that is ./sbom/scan-running.sh)"
say "An SBOM lists what is inside the software. We generate it two ways, then scan it for known CVEs."
predict "Before scanning, write down which of the top CVEs you expect to be FIRMWARE-RESIDENT (run on the chip) vs BUILD-HOST/TOOLCHAIN-ONLY (present in this tree but never shipped). This scope call is the graded insight."

step "1 — SPDX format (syft)"
say "SPDX is one standard SBOM schema; generate it from the sample directory."
run syft dir:vuln-sample -o spdx-json=out/sbom.spdx.json -q
observe "Wrote out/sbom.spdx.json — the component inventory in SPDX fields."

step "2 — CycloneDX format (syft)"
say "CycloneDX is a different standard schema for the SAME components — good for comparing how each represents a part."
run syft dir:vuln-sample -o cyclonedx-json=out/sbom.cdx.json -q
observe "Wrote out/sbom.cdx.json. Diff the two files: field names and component identifiers differ even though the inventory is the same."

step "Component counts (may differ between schemas)"
say "A quick count shows the two schemas don't even agree on how many 'components' there are."
SPDX_N="$(python3 -c 'import json;print(len(json.load(open("out/sbom.spdx.json")).get("packages",[])))')"
CDX_N="$(python3 -c 'import json;print(len(json.load(open("out/sbom.cdx.json")).get("components",[])))')"
observe "SPDX packages: $SPDX_N   CycloneDX components: $CDX_N  (a concrete SPDX-vs-CycloneDX difference for your notebook)."

step "3 — Vulnerabilities (grype, reading your SBOM)"
say "Scan the SBOM you just generated for known CVEs; being LISTED means the component version carries a CVE — not that the code path runs here."
note '$ grype sbom:out/sbom.spdx.json -o table | tee out/grype.txt'
grype_progress out/grype.txt yes sbom:out/sbom.spdx.json -o table
observe "Read the table top-down: CVE, severity, component, version. Mark each top CVE firmware-resident vs build-host-only. The scan does NOT establish reachability."

step "4 — Connect to the analyzer:  analyzer <-> SBOM <-> CWE <-> CVE"
say "The CWE class is what joins a scanner CVE to the firmware's code and the analyzer's evidence. The bridge splits findings into observable-in-the-twin vs library-only."
note '$ grype sbom:out/sbom.spdx.json -o json > out/grype.json'
grype_progress out/grype.json no sbom:out/sbom.spdx.json -o json
note '$ python3 cwe_bridge.py out/grype.json --all | tee out/cwe-bridge.txt'
python3 cwe_bridge.py out/grype.json --all | tee out/cwe-bridge.txt
observe "Observable classes have vulnerable firmware code the analyzer can show; library-only classes are real CVEs with no reachable code here. An SBOM hit is a QUESTION, not a verdict."

finding "Record: one concrete SPDX-vs-CycloneDX difference (counts above), your top-5 CVE table, one CVE summarized from the NVD, and — the graded part — which scope your SBOM covered (firmware-resident vs build-host) and what it therefore could NOT have caught (reachability). Outputs: out/sbom.spdx.json, out/sbom.cdx.json, out/grype.txt, out/grype.json, out/cwe-bridge.txt."
