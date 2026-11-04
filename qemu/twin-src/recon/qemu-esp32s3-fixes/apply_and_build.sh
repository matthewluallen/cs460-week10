#!/usr/bin/env bash
# apply_and_build.sh — reproduce the TBD StamPLC QEMU twin from a clean checkout.
#
# Clones the Espressif QEMU fork, overlays the TBD device-model files that live
# next to this script (hw_gpio/ hw_i2c/ hw_xtensa/ hw_misc/ include/), wires the two
# TBD I2C sources into meson, and builds qemu-system-xtensa. Idempotent; safe to
# re-run. Used by the Codespaces devcontainer (.devcontainer/postCreate.sh) and CI
# (.github/workflows/twin-ci.yml), and runnable by hand.
#
#   FORK_URL / FORK_BRANCH / QEMU_DIR override the defaults below.
set -euo pipefail

FORK_URL="${FORK_URL:-https://github.com/espressif/qemu.git}"
FORK_BRANCH="${FORK_BRANCH:-esp-develop}"      # fork is QEMU 9.2.2-based
QEMU_DIR="${QEMU_DIR:-$HOME/qemu-esp}"
# never allow a root-level / unwritable build dir (Codespaces postCreate
# has run with HOME unset, collapsing this to "/qemu-esp").
case "$QEMU_DIR" in ""|"/"|"/qemu-esp") QEMU_DIR="${HOME:-/home/$(id -un)}/qemu-esp";; esac
FIXES_DIR="$(cd "$(dirname "$0")" && pwd)"     # this dir = twin/qemu-esp32s3-fixes

echo "[tbd] fork=$FORK_URL@$FORK_BRANCH  ->  $QEMU_DIR"
if [ ! -e "$QEMU_DIR/hw/xtensa/esp32s3.c" ]; then
  git clone --depth 1 --branch "$FORK_BRANCH" "$FORK_URL" "$QEMU_DIR"
fi

python3 "$FIXES_DIR/patch_deadline_fence.py" "$QEMU_DIR"

echo "[tbd] overlaying TBD device-model files"
install -m644 "$FIXES_DIR/hw_xtensa/esp32s3_clk.c" "$QEMU_DIR/hw/xtensa/esp32s3_clk.c"
install -m644 "$FIXES_DIR/include/esp32s3_clk_defs.h" "$QEMU_DIR/include/hw/xtensa/esp32s3_clk_defs.h"
install -m644 "$FIXES_DIR/include/esp32s3_clk.h" "$QEMU_DIR/include/hw/xtensa/esp32s3_clk.h"
install -m644 "$FIXES_DIR/hw_xtensa/esp32s3.c"    "$QEMU_DIR/hw/xtensa/esp32s3.c"
install -m644 "$FIXES_DIR/hw_i2c/esp32_i2c.c"     "$QEMU_DIR/hw/i2c/esp32_i2c.c"
install -m644 "$FIXES_DIR/hw_i2c/tbd_unit_identity.c" "$QEMU_DIR/hw/i2c/tbd_unit_identity.c"
install -m644 "$FIXES_DIR/hw_i2c/tbd_i2c.c"     "$QEMU_DIR/hw/i2c/tbd_i2c.c"
install -m644 "$FIXES_DIR/hw_i2c/tbd_i2c.h"     "$QEMU_DIR/hw/i2c/tbd_i2c.h"
install -m644 "$FIXES_DIR/hw_i2c/qemu_i2c_glue.c" "$QEMU_DIR/hw/i2c/qemu_i2c_glue.c"
install -m644 "$FIXES_DIR/hw_gpio/esp32_gpio.c"   "$QEMU_DIR/hw/gpio/esp32_gpio.c"
install -m644 "$FIXES_DIR/hw_misc/esp32c3_jtag.c" "$QEMU_DIR/hw/misc/esp32c3_jtag.c"
install -m644 "$FIXES_DIR/hw_char/esp32_uart.c" "$QEMU_DIR/hw/char/esp32_uart.c"
install -m644 "$FIXES_DIR/include/esp32_uart.h" "$QEMU_DIR/include/hw/char/esp32_uart.h"
install -m644 "$FIXES_DIR/include/esp32_i2c.h"    "$QEMU_DIR/include/hw/i2c/esp32_i2c.h"
install -m644 "$FIXES_DIR/include/esp32_gpio.h"   "$QEMU_DIR/include/hw/gpio/esp32_gpio.h"
install -m644 "$FIXES_DIR/include/esp32c3_jtag.h" "$QEMU_DIR/include/hw/misc/esp32c3_jtag.h"

install -m644 "$FIXES_DIR/include/tbd_bridge.h" "$QEMU_DIR/include/hw/i2c/tbd_bridge.h"

# Register host observations without duplicating events on repeated overlays.
TRACE_FILE="$QEMU_DIR/hw/i2c/trace-events" TRACE_SOURCE="$FIXES_DIR/hw_i2c/observe-trace-events" python3 - <<'PYTRACE'
import os
from pathlib import Path
p=Path(os.environ['TRACE_FILE'])
lines=[line for line in p.read_text().splitlines() if not line.startswith('esp32_i2c_observe(')]
p.write_text('\n'.join(lines)+'\n'+Path(os.environ['TRACE_SOURCE']).read_text())
PYTRACE

echo "[tbd] wiring TBD I2C sources into hw/i2c/meson.build (insert before the source-set query)"
# meson consumes i2c_ss at `system_ss.add_all(... i2c_ss)`; any .add AFTER that line
# fails with "Tried to use 'add' after querying the source set", so INSERT (don't append)
# the S3-guarded entries just before that query. Idempotent.
MB="$QEMU_DIR/hw/i2c/meson.build" python3 - <<'PY'
import os
mb=os.environ["MB"]
lines=open(mb).read().splitlines()
need=[("CONFIG_XTENSA_ESP32S3","esp32_i2c.c"),
      ("CONFIG_XTENSA_ESP32S3","tbd_i2c.c"),
      ("CONFIG_XTENSA_ESP32S3","qemu_i2c_glue.c"),
      ("CONFIG_XTENSA_ESP32S3","tbd_unit_identity.c")]
def present(cfg,src): return any((cfg in l) and ("files('%s')"%src in l) for l in lines)
add=["i2c_ss.add(when: '%s', if_true: files('%s'))"%(c,s) for c,s in need if not present(c,s)]
if not add:
    print("meson: already wired"); raise SystemExit
out=[]; done=False
for l in lines:
    if (not done) and l.lstrip().startswith("system_ss.add_all") and "i2c_ss" in l:
        out.extend(add); done=True
    out.append(l)
if not done: out.extend(add)  # fallback, shouldn't happen
open(mb,"w").write("\n".join(out)+"\n")
print("meson: inserted %d line(s) before the query"%len(add))
PY

# Reuse upstream PCA9548 channel routing, including its per-channel buses.
KCONFIG="$QEMU_DIR/hw/xtensa/Kconfig" python3 - <<'PYMUX'
import os
from pathlib import Path
p=Path(os.environ["KCONFIG"]);s=p.read_text()
start=s.index('config XTENSA_ESP32S3\n');end=s.find('\nconfig ',start+1)
if end<0:end=len(s)
block=s[start:end]
for feature in ('PCA954X', 'SMBUS'):
    if 'select '+feature not in block:
        block=block.replace('    bool\n','    bool\n    select '+feature+'\n',1)
s=s[:start]+block+s[end:]
p.write_text(s)
PYMUX

echo "[tbd] configure + build (this takes ~10-15 min on a fresh tree)"
cd "$QEMU_DIR"
./configure \
  --target-list=xtensa-softmmu --enable-gcrypt --enable-slirp \
  --disable-werror \
  --disable-docs --disable-tools --disable-curl --disable-gnutls --disable-curses --disable-vnc --disable-gtk --disable-sdl --disable-png --disable-brlapi --disable-vte --disable-opengl --disable-spice
ninja -C build qemu-system-xtensa && strip build/qemu-system-xtensa

QB="$QEMU_DIR/build/qemu-system-xtensa"
echo "[tbd] built: $QB"
"$QB" --version | head -1
"$QB" -M help | grep -i esp32s3 && echo "[tbd] OK: esp32s3 machine registered"
