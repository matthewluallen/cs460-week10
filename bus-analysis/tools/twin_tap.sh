#!/usr/bin/env bash
# twin_tap.sh — run the twin with the virtual I²C tap and produce a sigrok capture.
# Output: bus.vcd (open in PulseView) + bus.json (Load capture in i2c-analyzer.html).
# Uses the prebuilt twin fetched by ../../qemu/fetch-qemu.sh, or set QEMU/FLASH_IMG.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"            # bus-analysis/tools
ROOT="$(cd "$HERE/../.." && pwd)"
TWIN="$ROOT/qemu/twin"
case "$(uname -s)/$(uname -m)" in
  Linux/x86_64)  B="$TWIN/qemu-twin-linux-x86_64";;
  Linux/aarch64) B="$TWIN/qemu-twin-linux-arm64";;   # local Dev Container on Apple Silicon
  Darwin/arm64)  B="$TWIN/qemu-twin-macos-arm64";;
  *) B="";;
esac
QEMU="${QEMU:-$B/qemu-system-xtensa}"; PCB="${PCB:-$B/pc-bios}"
IMG="${FLASH_IMG:-$TWIN/stamplc-flash-8MB.bin}"
if [ ! -x "$QEMU" ] || [ ! -f "$IMG" ]; then
  echo "twin not fetched — run ./qemu/fetch-qemu.sh first, or set QEMU, PCB and FLASH_IMG."; exit 1
fi
BUS="${BUS:-i2c0}"; RUN="${RUN:-5}"; OUT="${OUT:-bus}"
TRACE="$(mktemp)"; SER="$(mktemp -u)"
# Capture on a throwaway COPY of the flash image so this can run alongside the
# Step-0 twin (./qemu/run.sh) without QEMU's image write-lock clashing; writes
# during the tap are discarded. Bind the W5500 bridge on 127.0.0.2 so its service
# ports don't collide with a running Step-0 twin on 127.0.0.1.
WORKIMG="$(mktemp -u)-tap.bin"; cp "$IMG" "$WORKIMG"
trap 'rm -f "$WORKIMG" "$TRACE"' EXIT
echo "tapping $BUS for ${RUN}s (slowed, -icount shift=5,sleep=on) ..."
env TBD_W5_BIND=127.0.0.2 \
  "$QEMU" -L "$PCB" -M esp32s3 -m 8M -drive file="$WORKIMG",if=mtd,format=raw \
  --trace enable=esp32_i2c_observe -D "$TRACE" -icount shift=5,sleep=on \
  -serial null -serial null -serial "unix:$SER,server=on,wait=off" \
  -nographic -monitor none -no-reboot & QPID=$!
sleep "$RUN"; kill "$QPID" 2>/dev/null || true; wait "$QPID" 2>/dev/null || true
python3 "$HERE/trace_to_sigrok.py" "$TRACE" --bus "$BUS" --out "$OUT"
rm -f "$TRACE"
echo "wrote $OUT.vcd + $OUT.json"
echo "  → Load capture ($OUT.json) in bus-analysis/i2c-analyzer.html"
echo "  → or:  sigrok-cli -i $OUT.vcd -P i2c:scl=SCL:sda=SDA -A i2c   (same answers as a bench LA)"
