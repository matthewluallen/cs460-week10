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

step "Packaging bootloader + partitions + app into one 8 MB image"
say  "The twin boots a single flash image. We merge the just-built pieces at the"
say  "documented offsets (bootloader@0x0, partitions@0x8000, boot_app0@0xe000, app@0x10000)"
say  "and pad to 8 MB — exactly the layout a real StampLC is flashed with."
# boot_app0.bin location varies: older pioarduino put it under the project's
# .pio/packages, current builds keep it only in the global ~/.platformio. The
# trailing `|| true` matters: under `set -euo pipefail` a failing ls/find in a
# command substitution aborts the whole script (RC 2) BEFORE the fallback runs.
BOOTAPP0="$(ls "$FW"/.pio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin 2>/dev/null | head -1 || true)"
[ -n "$BOOTAPP0" ] || BOOTAPP0="$(find "$FW/.pio" "$HOME/.platformio" -name boot_app0.bin 2>/dev/null | head -1 || true)"
for f in "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin" "$BOOTAPP0"; do
  [ -f "$f" ] || { echo "missing build artifact: $f"; exit 1; }
done
# locate esptool (PlatformIO ships it; fall back to a module invocation)
if command -v esptool.py >/dev/null 2>&1; then ESPTOOL=(esptool.py)
elif python3 -c 'import esptool' 2>/dev/null;  then ESPTOOL=(python3 -m esptool)
else ESPTOOL=(pio pkg exec -- esptool.py); fi
mkdir -p "$HERE/twin"
run "${ESPTOOL[@]}" --chip esp32s3 merge_bin -o "$OUT" \
    --flash_mode dio --flash_freq 80m --flash_size 8MB \
    0x0 "$BUILD/bootloader.bin" 0x8000 "$BUILD/partitions.bin" \
    0xe000 "$BOOTAPP0" 0x10000 "$BUILD/firmware.bin"
# esptool 5.x merge_bin no longer pads the output to --flash_size, so it emits a
# ~1.8 MB file. The QEMU twin's SPI-flash (if=mtd) only accepts a 2/4/8/16 MB image
# ("Drive size error"), so pad to a full 8 MB with 0xFF (the erased-flash value).
WANT=$((8*1024*1024)); HAVE=$(wc -c < "$OUT")
if [ "$HAVE" -lt "$WANT" ]; then
  python3 -c "import sys; f=open('$OUT','ab'); f.write(b'\xff'*($WANT-$HAVE)); f.close()"
fi
observe "wrote $(basename "$OUT")  ($(wc -c < "$OUT") bytes)"
say  "Boot it:   ./qemu/run.sh $PROFILE      Control it:   ./qemu/console.sh"
note "The secure build ships NO default password — enroll one with 'passwd <12+ chars>' at the"
note "plc> prompt (first-run enrollment), then 'login operator <pass>'. Hold-A-at-boot clears it."
