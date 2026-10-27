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

echo "== Scanning with cve-bin-tool via the json-mirror data source (no NVD key needed) =="
# json-mirror = cve-bin-tool's public CVE snapshot, pulled with gsutil from a public GCS bucket.
# It does NOT use the NVD API, so it needs no key and works even when nvd.nist.gov is down or
# rate-limited. GAD (the GitLab advisory feed) is disabled: its multi-document YAML crashes
# cve-bin-tool's parser. (Have an NVD key and want the live feed instead? cve-bin-tool "$BIN" --nvd-api-key <key>)
command -v gsutil >/dev/null 2>&1 || pip install --quiet gsutil 2>/dev/null || pip install --quiet --break-system-packages gsutil 2>/dev/null || true
PATH="$HOME/.local/bin:$PATH" cve-bin-tool "$BIN" -n json-mirror --disable-data-source GAD -f html -o out/cve-bin-tool-firmware.html || \
  echo "   (scan did not complete — the mirror download needs open network; the strings proof above still stands)"

echo
echo "Done. Open out/cve-bin-tool-firmware.html."
echo "Compare with Exercise 2:"
echo "  - source SBOM (syft on firmware/)  -> the build's library pins, little CVE signal"
echo "  - binary scan (this)               -> the firmware-RESIDENT components actually on the chip"
echo "NOTE: on this ESP-IDF image cve-bin-tool typically detects 0 products — it fingerprints by"
echo "version strings the monolithic build does not expose, even though 'strings' proves mbedTLS/"
echo "lwIP ARE present. That gap is the finding: a binary scan missing a component is not the same"
echo "as the component being absent (absent-from-scan != absent-from-device)."
