#!/usr/bin/env bash
# build.sh — build one firmware profile without typing the full PlatformIO line (Part 9).
#
#   ./remediation/build.sh vuln      # pio run -e m5stamplc        (ENABLE_CWE_DEMOS: teaching weaknesses)
#   ./remediation/build.sh secure    # pio run -e m5stamplc_secure (ENABLE_CWE_DEMOS removed; SECURE_BUILD)
#
# The two profiles differ only by the build switch (see sbom/firmware/platformio.ini):
#   m5stamplc_secure = m5stamplc  build_unflags=-DENABLE_CWE_DEMOS  build_flags+=-DSECURE_BUILD
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

FW="$(cd "$HERE/../sbom/firmware" && pwd)"

PROFILE="${1:-}"
case "$PROFILE" in
  vuln|vulnerable|m5stamplc)          ENV="m5stamplc" ;;
  secure|hardened|m5stamplc_secure)   ENV="m5stamplc_secure" ;;
  *) echo "usage: $0 <vuln|secure>"; exit 2 ;;
esac

step "Build the '$ENV' firmware profile (Part 9)"
if [ "$ENV" = "m5stamplc_secure" ]; then
  say "Building the HARDENED profile: ENABLE_CWE_DEMOS is unflagged and SECURE_BUILD is"
  say "defined, so the compiler selects the #else (remediation) branch of each demo."
else
  say "Building the TEACHING profile: ENABLE_CWE_DEMOS is present, so the intentional"
  say "weakness branch of each demo is compiled in."
fi
note "Building is NOT running. This step only produces a firmware artifact; the"
note "weakness/hardening is only demonstrated once you RUN that image in the twin."
say  "The QEMU twin is your lab environment; physical hardware is reference/validation only."

cd "$FW"
run pio run -e "$ENV"

OUT="$FW/.pio/build/$ENV/firmware.bin"
echo
if [ -f "$OUT" ]; then
  observe "Built: $OUT ($(wc -c < "$OUT") bytes)."
else
  observe "Built env '$ENV' (expected artifact under $FW/.pio/build/$ENV/firmware.bin)."
fi
observe "A build label alone is not evidence of behavior - prove it by running it."
if [ "$ENV" = "m5stamplc_secure" ]; then
  note "Next: run this profile in the twin, then re-run the attacks to watch them fail:"
  note "  ./qemu/run.sh secure            # boot the secure profile (profile switch wired by the lead)"
  note "  ./remediation/verify-secure.sh  # re-run Part 6-8 attacks; each should be REFUSED"
  finding "Record the source commit, effective build profile, and artifact hash for this"
  finding "arm - then the runtime observations you get from ./qemu/run.sh secure."
else
  note "Next: boot it in the twin to establish the vulnerable baseline:"
  note "  ./qemu/run.sh                   # boot the teaching profile"
fi
