#!/usr/bin/env bash
# Firmware reverse-engineering (read-only static analysis) - the image half of the SBOM Part.
# Runs the four steps against the image your twin is running (or a path you give) and narrates each.
# Verified in the cs460-week10 Codespace (esptool 5.6, binwalk, xtensa toolchain objdump).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Which image: an argument (an app .bin or a full flash image such as qemu/twin/stamplc-secure-8MB.bin),
# else the image your running twin booted, else the committed prebuilt app.
F="${1:-}"
if [ -z "$F" ]; then
  # shellcheck source=../qemu/twin-status.sh
  . "$ROOT/qemu/twin-status.sh"
  if twin_detect "${SERIAL_SOCK:-/tmp/plc-serial}" 0 && [ -n "${TWIN_IMG:-}" ]; then
    F="$TWIN_IMG"; echo "(analyzing the image your running twin booted: $F)"
  else
    F="$ROOT/sbom/firmware/prebuilt/firmware.bin"; echo "(no twin running - analyzing the committed prebuilt app: $F)"
  fi
fi
[ -f "$F" ] || { echo "firmware not found: $F"; exit 1; }
# A full flash image (bootloader + partition table + apps) is not an app image: take the slot the
# bootloader selects, and say which one, so every step below reads the code that boots.
if python3 -c 'import sys; f=open(sys.argv[1],"rb"); f.seek(0x8000); sys.exit(0 if f.read(2)==b"\xaa\x50" else 1)' "$F" 2>/dev/null; then
  python3 "$ROOT/qemu/fw_identity.py" "$F" | sed 's/^/   /'
  APP="$(mktemp --suffix=-app.bin)"; trap 'rm -f "$APP"' EXIT
  python3 "$ROOT/qemu/fw_identity.py" --extract-app "$APP" "$F" | sed 's/^/   /'
  FLASH="$F"; F="$APP"
fi

echo "########## 1. Image structure - esptool image-info ##########"
# What kind of image is this? chip, segments, load addresses, entry point, build id.
( esptool --chip esp32s3 image-info "$F" 2>/dev/null || esptool.py --chip esp32s3 image-info "$F" )

echo; echo "########## 2. Baked-in secrets - strings ##########"
# A firmware blob is not opaque: ASCII literals fall straight out of it.
strings -n 6 "$F" | grep -iE 'openplc|admin|passwd|password|secret|signing-key|cwe-798' | head -20

echo; echo "########## 3. Embedded keys / certs / crypto - binwalk ##########"
if ! command -v binwalk >/dev/null 2>&1 || ! binwalk --help >/dev/null 2>&1; then
  echo "(installing a working binwalk from apt - the pip build in the image is broken)"
  sudo apt-get -y -q install binwalk >/dev/null 2>&1 || true
fi
/usr/bin/binwalk "$F" 2>/dev/null || binwalk "$F"

echo; echo "########## 4. Xtensa disassembly of the IROM code - objdump ##########"
# The firmware is ESP32-S3 (Xtensa). radare2 here has no xtensa plugin, so use the
# cross-objdump PlatformIO already installed. Carve the IROM region and disassemble it.
OD="$(find "$HOME/.platformio" -name 'xtensa-esp*-objdump' 2>/dev/null | head -1)"
[ -n "$OD" ] || { echo "xtensa objdump not found (build the firmware once, or install the ESP toolchain)"; exit 0; }
dd if="$F" bs=4096 skip=96 count=1 of=/tmp/irom.bin 2>/dev/null   # IROM segment region (~file 0x60000)
"$OD" -D -b binary -m xtensa --adjust-vma=0x42000020 /tmp/irom.bin | sed -n '8,40p'
echo
echo "Done. Addresses in step 4 are relative to the IROM segment base (0x42000020 from step 1)."
echo "To go deeper with symbols, load firmware.elf (after 'pio run') or the segments at their"
echo "step-1 load addresses into Ghidra's Xtensa loader."
