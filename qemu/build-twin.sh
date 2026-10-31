#!/usr/bin/env bash
# build-twin.sh — build one firmware PROFILE from source and package it into an
# 8 MB flash image you can boot in the QEMU twin. Use this to run the SECURE
# build (Part 9) alongside the vulnerable one and compare them like-for-like.
#
#   ./qemu/build-twin.sh secure     # -> qemu/twin/stamplc-secure-8MB.bin
#   ./qemu/build-twin.sh vuln       # -> qemu/twin/stamplc-vuln-8MB.bin
#
# Then:  RESTART=1 ./qemu/run.sh secure   (in one terminal)  and  ./qemu/console.sh  (in another)
#        (RESTART=1 replaces a twin that is already running; without it run.sh reuses that twin)
# The twin is your test bench — no hardware. The console you get is the SAME plc>
# console you'd get over USB on the real StampLC; every command works identically.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || { step(){ echo "== $* =="; }; say(){ echo "» $*"; }; run(){ echo "+ $*"; "$@"; }; observe(){ echo "→ $*"; }; note(){ echo "  $*"; }; }

PROFILE="${1:-secure}"
case "$PROFILE" in
  secure|hardened) ENV=m5stamplc_secure; OUT="$HERE/twin/stamplc-secure-8MB.bin";;
  vuln|vulnerable) ENV=m5stamplc;        OUT="$HERE/twin/stamplc-vuln-8MB.bin";;
  *) echo "usage: ./qemu/build-twin.sh [secure|vuln]"; exit 2;;
esac
FW="$ROOT/sbom/firmware"
BUILD="$FW/.pio/build/$ENV"

step "Building the $PROFILE firmware ($ENV) from source"
say  "PlatformIO compiles sbom/firmware with the $ENV profile. The two profiles differ"
say  "by ONE build flag (ENABLE_CWE_DEMOS on vs off) — same source tree, so the twin boots"
say  "either one. Building is not running: you'll run the result in the twin next."
if ! command -v pio >/dev/null 2>&1; then
  echo "pio not found — open this repo in a Codespace / Dev Container (it installs PlatformIO)."; exit 1
fi
run pio run -d "$FW" -e "$ENV"

step "Packaging bootloader + partitions + factory project + app into one 8 MB image"
say  "The twin boots a single flash image. We place the just-built pieces at the documented"
say  "offsets (bootloader@0x0, partitions@0x8000, NVS@0x9000, boot_app0@0xe000, app@0x10000)"
say  "and pad to 8 MB with 0xFF (erased flash) — the layout a real StampLC is flashed with."
say  "The NVS partition carries the factory default project (motor start/stop + E-stop): the"
say  "same bytes the Studio would load, stored where the loader keeps them (namespace plc, key"
say  "project_v1). So the image boots with that program installed, in PROGRAM mode."
# scripts/factory_image.py does the merge in plain Python (no esptool needed) and writes the
# NVS partition itself; it finds boot_app0.bin under .pio or ~/.platformio.
mkdir -p "$HERE/twin"
run python3 "$FW/scripts/factory_image.py" merge --build-dir "$BUILD" --out "$OUT" --pad-8mb
observe "wrote $(basename "$OUT")  ($(wc -c < "$OUT") bytes)"
say  "Boot it:   ./qemu/run.sh $PROFILE      Control it:   ./qemu/console.sh"
note "The secure build ships NO default password — enroll one with 'passwd <12+ chars>' at the"
note "plc> prompt (first-run enrollment), then 'login operator <pass>'. Hold-A-at-boot clears it."
