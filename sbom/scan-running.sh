#!/usr/bin/env bash
# scan-running.sh — Part 5: SBOM + vulnerability scan of the firmware your twin is RUNNING,
# and of the plant's HMI container if the plant is up.
#
#   ./sbom/scan-running.sh                 # the image the running twin booted (else the default image)
#   ./sbom/scan-running.sh <flash.bin>     # any flash image, e.g. one read off a board (esptool read_flash)
#
# Steps (each narrated):
#   1. fingerprint  the running twin's flash image -> boot slot -> app ELF SHA-256; and, if the console
#                   answers, the firmware's own `plc> diag json` elf_sha256 (a self-report) to compare
#   2. match        that fingerprint to a build in this checkout (local PlatformIO build or the prebuilt)
#   3. SBOM         firmware: DECLARED (platformio.ini) + FOUND (app descriptor, in-image strings)
#                   -> out/firmware.cdx.json + out/firmware.spdx.json ; syft's own view of the app binary
#                   HMI container (cs460/hmi-nodered): syft -> out/hmi.cdx.json + out/hmi.spdx.json
#   4. scan         grype on both SBOMs (+ trivy on the firmware SBOM if installed)
#   5. RISK         one table sorted by grype's risk score (EPSS forecast + CISA KEV + severity)
# Outputs: sbom/out/  (firmware.*, hmi.*, grype-*.json/.txt, risk.txt, running-identity.json, app.bin)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/.." && pwd)"
source "$ROOT/tools/teach.sh" 2>/dev/null || true
declare -F step    >/dev/null 2>&1 || step()    { printf '\n==== %s ====\n' "$*"; }
declare -F say     >/dev/null 2>&1 || say()     { printf '>  %s\n' "$*"; }
declare -F observe >/dev/null 2>&1 || observe() { printf -- '-> %s\n' "$*"; }
declare -F predict >/dev/null 2>&1 || predict() { printf '?  PREDICT FIRST: %s\n' "$*"; }
declare -F finding >/dev/null 2>&1 || finding() { printf 'FINDING: %s\n' "$*"; }
declare -F note    >/dev/null 2>&1 || note()    { printf '   %s\n' "$*"; }
OUT="$HERE/out"; mkdir -p "$OUT"
SERIAL_SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"
HMI_IMAGE="${HMI_IMAGE:-cs460/hmi-nodered}"

with_timer() {   # with_timer <outfile> <cmd...>   (grype's first run loads a large DB; show it is alive)
  local out="$1"; shift
  "$@" >"$out" 2>"$out.err" & local pid=$! t0=$SECONDS
  while kill -0 "$pid" 2>/dev/null; do printf '\r   working… %3ss (first grype run loads its vulnerability database)   ' "$((SECONDS - t0))"; sleep 1; done
  local rc=0; wait "$pid" || rc=$?
  printf '\r   done in %ss.                                                              \n' "$((SECONDS - t0))"
  grep -viE 'no explicit name|the default behavior' "$out.err" >&2 || true; rm -f "$out.err"
  return "$rc"
}

step "1 — Which firmware is running?"
IMG="${1:-}"
if [ -z "$IMG" ]; then
  # shellcheck source=../qemu/twin-status.sh
  . "$ROOT/qemu/twin-status.sh"
  if twin_detect "$SERIAL_SOCK" 0 && [ -n "${TWIN_IMG:-}" ]; then
    IMG="$TWIN_IMG"; say "Your twin (pid $TWIN_PID) booted: $IMG"
  else
    IMG="${FLASH_IMG:-$ROOT/qemu/twin/stamplc-flash-8MB.bin}"
    say "No twin is running, so this scans the image ./qemu/studio.sh would boot: $IMG"
    say "(start the twin first if you want the RUNNING image: ./qemu/studio.sh)"
  fi
fi
[ -f "$IMG" ] || { echo "no image at $IMG — run ./qemu/fetch-qemu.sh, or pass a flash image path"; exit 1; }
predict "How many components will the SBOM list, and how many will carry a version a scanner can match? Which CVEs, if any, sit in code that runs on the chip?"

SELF="$OUT/diag-self-report.json"; rm -f "$SELF"
python3 - "$SELF" "${BRIDGE_CONSOLE_PORT:-8081}" <<'PY' || true
import json, re, socket, sys, time
out, port = sys.argv[1], int(sys.argv[2])
try:
    s = socket.create_connection(("127.0.0.1", port), timeout=2)
except OSError:
    print("   (console not reachable on :%d — fingerprint from the image only)" % port); sys.exit(0)
s.settimeout(0.3); s.sendall(b"diag json\n"); buf = b""; end = time.time() + 4
while time.time() < end and b'"elf_sha256"' not in buf:
    try: buf += s.recv(4096)
    except socket.timeout: pass
s.close()
m = re.search(rb'\{"fw":.*?\}', buf)
if m:
    try:
        j = json.loads(m.group(0)); json.dump(j, open(out, "w"))
        print("   asked the controller: plc> diag json -> elf_sha256 %s (the firmware's own report)" % j.get("elf_sha256", "?")[:16])
    except ValueError:
        print("   (diag json reply not parseable)")
else:
    print("   (no diag json reply on the console)")
PY

step "2 — Fingerprint, build match, and the firmware SBOM (SPDX + CycloneDX)"
say "DECLARED = what platformio.ini says was compiled in. FOUND = what the image itself shows. Keep them apart."
python3 "$HERE/firmware_sbom.py" --image "$IMG" --out "$OUT" ${SELF:+--self-report "$SELF"}
observe "Wrote out/firmware.cdx.json and out/firmware.spdx.json (same components, two schemas), out/app.bin (the boot slot) and out/running-identity.json."

if command -v syft >/dev/null 2>&1; then
  step "3 — A generic scanner's view of the same app binary (syft)"
  say "syft catalogs package ecosystems. A monolithic microcontroller image has none it recognises, so expect little or nothing."
  syft scan "file:$OUT/app.bin" -q -o cyclonedx-json="$OUT/app-syft.cdx.json" 2>/dev/null \
    || syft "file:$OUT/app.bin" -q -o cyclonedx-json="$OUT/app-syft.cdx.json" 2>/dev/null || true
  N="$(python3 -c 'import json,sys;print(len(json.load(open(sys.argv[1])).get("components",[])))' "$OUT/app-syft.cdx.json" 2>/dev/null || echo '?')"
  observe "syft found $N component(s) in the binary — compare with the list above. Absent from a scan is not absent from the device."

  step "4 — The plant's HMI container image ($HMI_IMAGE)"
  if command -v docker >/dev/null 2>&1 && docker image inspect "$HMI_IMAGE" >/dev/null 2>&1; then
    syft "$HMI_IMAGE" -q -o spdx-json="$OUT/hmi.spdx.json" -o cyclonedx-json="$OUT/hmi.cdx.json"
    observe "Wrote out/hmi.spdx.json + out/hmi.cdx.json — the operator screen's software, a very different supply chain."
  else
    note "No local image $HMI_IMAGE — bring the plant up first (stations/README.md), then re-run. Skipping the HMI."
  fi
else
  note "syft is not installed (see sbom/README.md) — skipping the binary view and the HMI container."
fi

if command -v grype >/dev/null 2>&1; then
  step "5 — Vulnerabilities (grype reads the SBOMs)"
  say "Being LISTED means a component version carries a known CVE — not that the weak code runs here or is reachable."
  with_timer "$OUT/grype-firmware.json" grype "sbom:$OUT/firmware.cdx.json" -o json || true
  grype "sbom:$OUT/firmware.cdx.json" -o table >"$OUT/grype-firmware.txt" 2>/dev/null || true
  if [ -f "$OUT/hmi.spdx.json" ]; then
    with_timer "$OUT/grype-hmi.json" grype "sbom:$OUT/hmi.spdx.json" -o json || true
    grype "sbom:$OUT/hmi.spdx.json" -o table >"$OUT/grype-hmi.txt" 2>/dev/null || true
  fi
  if command -v trivy >/dev/null 2>&1; then
    trivy sbom "$OUT/firmware.cdx.json" -q --format table >"$OUT/trivy-firmware.txt" 2>/dev/null \
      && note "second opinion: out/trivy-firmware.txt (trivy on the same firmware SBOM)" || true
  fi

  step "6 — RISK, not SEVERITY"
  say "grype's risk score blends EPSS (the daily forecast that a CVE is exploited in the next 30 days), CISA's Known Exploited Vulnerabilities list, and severity."
  python3 - "$OUT" <<'PY' | tee "$OUT/risk.txt"
import json, os, sys
out = sys.argv[1]
rows = []
for scope, fn in (("firmware", "grype-firmware.json"), ("HMI container", "grype-hmi.json")):
    p = os.path.join(out, fn)
    if not os.path.isfile(p):
        continue
    try:
        d = json.load(open(p))
    except ValueError:
        continue
    for m in d.get("matches", []):
        v = m.get("vulnerability", {}); a = m.get("artifact", {})
        epss = v.get("epss") or []
        e = max((x.get("epss", 0) for x in epss), default=None)
        kev = bool(v.get("knownExploited") or v.get("kev"))
        fix = ",".join((v.get("fix") or {}).get("versions") or []) or "-"
        rows.append((v.get("risk") or 0.0, scope, v.get("id", "?"), a.get("name", "?"), a.get("version", "?"),
                     v.get("severity", "?"), e, kev, fix))
rows.sort(key=lambda r: (-(r[0] or 0), r[2]))
print("%-14s %-20s %-26s %-12s %-9s %7s %4s %6s  %s" % ("SCOPE", "VULNERABILITY", "PACKAGE", "VERSION", "SEVERITY", "EPSS%", "KEV", "RISK", "FIXED-IN"))
for r in rows[:25]:
    print("%-14s %-20s %-26s %-12s %-9s %7s %4s %6.1f  %s" % (r[1], r[2], r[3][:26], str(r[4])[:12], r[5],
          "-" if r[6] is None else "%.2f" % (100 * r[6]), "yes" if r[7] else "-", r[0] or 0, r[8]))
n_fw = sum(1 for r in rows if r[1] == "firmware")
print("\n%d findings in total (%d firmware, %d HMI container). Full tables: out/grype-firmware.txt, out/grype-hmi.txt"
      % (len(rows), n_fw, len(rows) - n_fw))
if n_fw == 0:
    print("Firmware: 0 matches is NOT 'no vulnerabilities': the bundled mbedTLS/lwIP/FreeRTOS carry no version "
          "string in the image, so nothing could be matched. That gap is a finding.")
PY
  if [ -f "$OUT/grype-firmware.json" ] && [ -f "$HERE/cwe_bridge.py" ]; then
    SRC="$OUT/grype-firmware.json"; [ -f "$OUT/grype-hmi.json" ] && SRC="$OUT/grype-hmi.json"
    python3 "$HERE/cwe_bridge.py" "$SRC" --all >"$OUT/cwe-bridge.txt" 2>/dev/null \
      && note "CWE bridge (observable in the twin vs library-only): out/cwe-bridge.txt" || true
  fi
else
  note "grype is not installed (see sbom/README.md) — the SBOMs are written; the scan step is skipped."
fi

finding "SHOW the fingerprint (app ELF SHA-256, and whether plc> diag json agrees), the build match, one finding with its RISK, and which scope it is in (firmware on the chip vs HMI container). SAY whether it is reachable or only present. NOTE what the firmware scan could not see (components with no version)."
