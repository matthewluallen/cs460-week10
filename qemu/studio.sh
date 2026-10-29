#!/usr/bin/env bash
# Drive the twin from the Logic Studio in your browser — one command, one port.
#
#   ./qemu/studio.sh [vuln|secure|malicious]      (default: vuln)
#
# Starts the QEMU twin (if its console socket isn't already up) and the Studio
# server on :8080. The server both serves the Studio page AND bridges it to the
# twin, so the browser's "Connect twin" is same-origin — no URL to figure out.
#
#   • GitHub Codespaces : open the auto-forwarded port 8080 (popup, or the Ports tab).
#   • Local VS Code     : open http://localhost:8080
#
# Then click "Connect twin" in the Studio. Ctrl-C stops the server; the twin keeps
# running (stop it with:  pkill -f qemu-system-xtensa ).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PROFILE="${1:-vuln}"
SERIAL_SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"
PORT="${BRIDGE_PORT:-8080}"

if [ ! -S "$SERIAL_SOCK" ]; then
  echo "starting the twin ($PROFILE)…"
  SERIAL_SOCK="$SERIAL_SOCK" "$HERE/run.sh" "$PROFILE" >/tmp/twin.log 2>&1 &
  for _ in $(seq 1 100); do [ -S "$SERIAL_SOCK" ] && break; sleep 0.1; done
fi
[ -S "$SERIAL_SOCK" ] || { echo "twin did not come up — see /tmp/twin.log"; exit 1; }

echo "twin console   : $SERIAL_SOCK"
echo "Studio + bridge: http://localhost:$PORT   (Codespaces: open forwarded port $PORT)"
echo "text console   : ./qemu/console.sh        (attaches alongside the Studio)"
echo "In the Studio, click 'Connect twin'.  Ctrl-C stops the server (twin keeps running)."
exec python3 "$HERE/twin-bridge.py" --port "$PORT" --serial "$SERIAL_SOCK"
