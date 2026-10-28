#!/usr/bin/env bash
# Run the StampLC firmware in the prebuilt (de-branded) QEMU twin — no hardware.
# First run ./qemu/fetch-qemu.sh once to download the binary + firmware image.
# See ../docs/QEMU-Twin.md for the full story.
#
# ─ VIRTUAL TIME (icount) ─────────────────────────────────────────────────────
# A fixed shift=N assigns 2^N guest nanoseconds per instruction. It is not a
# simple wall-clock speed knob or cycle-accurate timing. With sleep=off, an idle
# CPU can jump to the next virtual timer. Host throughput depends on the workload.
#   ICOUNT=shift=3,align=off,sleep=off   (default fixed instruction-count mapping)
#   ICOUNT=shift=5,sleep=on             (different virtual-time/idle policy)
#   ICOUNT=off                          (host-driven timing; no fixed-deadline gate)
# The separate plant runner qualifies its exact clock/deadline behavior.
# Other knobs:
#   FLASH_IMG=<path>   8MB merged image (default: ./qemu/twin/stamplc-flash-8MB.bin)
#   W5_BIND=<ip>       host address the W5500 TCP bridge binds (default 127.0.0.1)
#   ALLOW_REBOOT=1     drop -no-reboot so ESP.restart() actually reboots
#                      (watch the OTA-flashed image boot); default keeps -no-reboot
#   SERIAL_SOCK / QMP_SOCK   unix socket paths (defaults /tmp/plc-serial, /tmp/plc-qmp)
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OS=$(uname -s); ARCH=$(uname -m)
case "$OS/$ARCH" in
  Linux/x86_64)  BDIR="$HERE/twin/qemu-twin-linux-x86_64" ;;
  Linux/aarch64) BDIR="$HERE/twin/qemu-twin-linux-arm64" ;;   # local Dev Container on Apple Silicon
  Darwin/arm64)  BDIR="$HERE/twin/qemu-twin-macos-arm64" ;;
  *) echo "Unsupported platform $OS/$ARCH — build from source (see ../docs/QEMU-Twin.md)."; exit 1 ;;
esac
# ── which firmware version to boot ──────────────────────────────────────────
# The QEMU twin is your test environment — no hardware needed. The physical
# StampLC is reference only (it validated all of this on real silicon); every
# console command you use here also works over USB on the real device.
# First argument picks the profile; or set FLASH_IMG=<path> to boot any image.
#   ./qemu/run.sh            # vulnerable teaching build (default)
#   ./qemu/run.sh secure     # hardened build (Part 9) — build it first: ./qemu/build-twin.sh secure
#   ./qemu/run.sh malicious  # supply-chain image (Part 7)
PROFILE="${1:-vuln}"
case "$PROFILE" in
  vuln|vulnerable|teaching) IMGDEF="$HERE/twin/stamplc-flash-8MB.bin";           WHICH="vulnerable teaching build";;
  secure|hardened)          IMGDEF="$HERE/twin/stamplc-secure-8MB.bin";          WHICH="hardened secure build";;
  malicious|evil)           IMGDEF="$HERE/twin/stamplc-flash-8MB-MALICIOUS.bin"; WHICH="supply-chain malicious image";;
  *) echo "usage: ./qemu/run.sh [vuln|secure|malicious]   (or set FLASH_IMG=<path>)"; exit 2;;
esac
IMG="${FLASH_IMG:-$IMGDEF}"
QEMU="$BDIR/qemu-system-xtensa"
if [ ! -x "$QEMU" ]; then
  echo "Twin not fetched yet (or wrong platform). Run:  ./qemu/fetch-qemu.sh"
  exit 1
fi
if [ ! -f "$IMG" ]; then
  case "$PROFILE" in
    secure|hardened) echo "No secure image at $IMG — build it once:  ./qemu/build-twin.sh secure";;
    malicious|evil)  echo "No malicious image at $IMG — run ./qemu/fetch-qemu.sh (Part 7 notes the fallback if unpublished).";;
    *)               echo "No twin image at $IMG — run:  ./qemu/fetch-qemu.sh";;
  esac
  exit 1
fi
echo "booting the $WHICH:  $(basename "$IMG")"

# ── virtual-clock throttle ──
ICOUNT="${ICOUNT:-shift=3,align=off,sleep=off}"
ICOUNT_ARGS=()
case "$ICOUNT" in
  off|none|OFF|NONE) : ;;                         # host-speed, non-deterministic
  *) ICOUNT_ARGS=(-icount "$ICOUNT") ;;
esac

# ── reboot behaviour (OTA demo) ──
REBOOT_ARGS=(-no-reboot)
[ "${ALLOW_REBOOT:-0}" = "1" ] && REBOOT_ARGS=()

SERIAL_SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"; QMP_SOCK="${QMP_SOCK:-/tmp/plc-qmp}"
W5_BIND="${W5_BIND:-127.0.0.1}"
rm -f "$SERIAL_SOCK" "$QMP_SOCK"

# ── least privilege: let the UNPRIVILEGED emulator bind the OTA port (80) ──
# The W5500 bridge binds the guest's listen ports on the host; 80 is a privileged
# (<1024) port. Codespaces forbids lowering net.ipv4.ip_unprivileged_port_start
# (read-only), and we do NOT want to run the whole emulator as root. So grant the
# qemu binary exactly ONE capability — cap_net_bind_service — and nothing more.
# Idempotent: skipped if already granted, or if setcap/sudo aren't available.
if command -v getcap >/dev/null 2>&1 && ! getcap "$QEMU" 2>/dev/null | grep -q cap_net_bind_service; then
  if command -v setcap >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    sudo setcap cap_net_bind_service=+ep "$QEMU" 2>/dev/null \
      && echo "      granted cap_net_bind_service to qemu (OTA :80 bindable, no root)" \
      || echo "      NOTE: couldn't grant cap_net_bind_service — OTA :80 may fail (see qemu/README.md)"
  fi
fi

echo "twin: icount='${ICOUNT}'  reboot=$([ ${#REBOOT_ARGS[@]} -eq 0 ] && echo on || echo off)  W5500->$W5_BIND"
echo "      serial=$SERIAL_SOCK  qmp=$QMP_SOCK   (console:  nc -U $SERIAL_SOCK )"
exec env TBD_W5_BIND="$W5_BIND" \
  "$QEMU" -L "$BDIR/pc-bios" -M esp32s3 -m 8M \
  -drive file="$IMG",if=mtd,format=raw \
  "${ICOUNT_ARGS[@]}" -accel tcg,thread=single \
  -serial null -serial null -serial "unix:$SERIAL_SOCK,server=on,wait=off" \
  -qmp "unix:$QMP_SOCK,server=on,wait=off" \
  -nographic -monitor none "${REBOOT_ARGS[@]}"
