#!/usr/bin/env bash
# console.sh — attach the StampLC plc> text console (used by the firmware/attack parts).
#
# In the self-drive model, ./qemu/studio.sh starts the twin AND the bridge, and the
# bridge multiplexes the console. This attaches to that muxed console, so the text
# console and the browser Studio run at the SAME time — no more single-client
# "stuck console" when both are open.
#
#   • If the bridge is running  → attach to it (coexists with the Studio).
#   • If only the twin is running (you ran ./qemu/run.sh directly, no Studio)
#                               → attach straight to the twin socket.
#
# To QUIT the console:  press  Ctrl-C   (or close the terminal pane).
set -euo pipefail
PORT="${BRIDGE_CONSOLE_PORT:-8081}"
SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"

echo "This is the StampLC plc> console — the SAME interface you'd get over USB on the"
echo "real device. Everything you do to the twin here, you'd do identically on hardware."
echo
echo "  Control:   mode run|prog|remote   io   force <tag> <0|1>   points"
echo "  Observe:   status   busstream on|off   bustrace on   clk   log"
echo "  Services:  cwe   proto   svcinfo                      (what this image exposes)"
echo "  Access:    login operator <pass>   passwd <new>       (secure build: enroll first)"
echo "  Panel:     btn a|b|c                                  (drive the on-device buttons/LCD)"
echo
echo "  (quit the console with:  Ctrl-C , or close the pane)"
echo

if command -v nc >/dev/null 2>&1 && nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
  echo "+ nc 127.0.0.1 $PORT    (bridge console — runs alongside the Studio)"
  echo
  exec nc 127.0.0.1 "$PORT"
elif [ -S "$SOCK" ]; then
  echo "+ nc -U $SOCK    (direct to the twin — no bridge running)"
  echo
  exec nc -U "$SOCK"
else
  echo "No twin is running yet. Start it first:"
  echo "    ./qemu/studio.sh        # twin + Studio + console, one command"
  exit 1
fi
