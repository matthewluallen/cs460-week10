#!/usr/bin/env bash
# Part 2, Exercise 3 — scan the COMPILED firmware binary for embedded components + CVEs.
#
# Exercise 2 read build *manifests* (a source SBOM). This reads the shipped artifact
# itself: cve-bin-tool fingerprints well-known components inside the .bin and maps them to
# CVEs — so it surfaces the ESP-IDF libraries that actually run on the chip (mbedTLS, lwIP,
# wpa_supplicant), which the source scan could not see.
#
# First run downloads the CVE database (a few minutes, then cached) — it needs network, so
# run it in your Codespace / dev container, not an offline box. The `strings` check below
# works with no network at all.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
BIN="firmware/prebuilt/stamplc-factory-8MB.bin"
[ -f "$BIN" ] || { echo "firmware binary not found: $BIN  (build it — see firmware/BUILD.md)"; exit 1; }

echo "============================================================"
echo " Binary CVE scan of the compiled firmware: $BIN"
echo "============================================================"

echo
echo "== Offline proof the components are really in the image (no network needed) =="
strings "$BIN" | grep -iE 'mbedtls|lwip|wpa_supplicant|esp-idf|esp-tls' | sort -u | head -20 || true
echo "   ^ these are the crypto + TCP/IP stacks the Part-2 note said 'most embedded CVEs live in'."

echo
if ! command -v cve-bin-tool >/dev/null 2>&1; then
  echo "cve-bin-tool is not installed. Install it, then re-run:"
  echo "    pipx install cve-bin-tool      # or:  pip install --break-system-packages cve-bin-tool"
  exit 0
fi

echo "== Scanning with cve-bin-tool (first run downloads the CVE DB — be patient) =="
# -f html writes a readable report; console output shows the table too.
# An NVD API key makes the CVE-DB download ~10x faster and avoids rate-limit stalls.
NVD_ARGS=""
if [ -n "${NVD_API_KEY:-}" ]; then
  NVD_ARGS="--nvd-api-key ${NVD_API_KEY}"
  echo "   (NVD_API_KEY detected — fast database path)"
else
  echo "   NOTE: no NVD_API_KEY set — the FIRST database build is slow and rate-limited."
  echo "         Free key: https://nvd.nist.gov/developers/request-an-api-key"
  echo "         then 'export NVD_API_KEY=<key>' (or add it as a Codespaces secret; see sbom/README.md Ex.3)."
fi
cve-bin-tool "$BIN" $NVD_ARGS -f html -o out/cve-bin-tool-firmware.html || \
  echo "   (scan did not complete — the CVE-DB download needs open network and, realistically, an NVD_API_KEY; see sbom/README.md)"

echo
echo "Done. Open out/cve-bin-tool-firmware.html."
echo "Compare with Exercise 2:"
echo "  - source SBOM (syft on firmware/)  -> the build's library pins, little CVE signal"
echo "  - binary scan (this)               -> the firmware-RESIDENT components actually on the chip"
echo "For one detected component: version? known NVD CVE? fix = firmware rebuild or architecture"
echo "control? Note reachability uncertainty — detected is not the same as exploitable."
