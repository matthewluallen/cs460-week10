#!/usr/bin/env bash
# Drive the twin from the Logic Studio in your browser — one command, one port.
#
#   ./qemu/studio.sh [vuln|secure|malicious]      (default: whatever twin is running, else vuln)
#
# Checks first, boots only if needed:
#   • a twin already running on the console socket → reused, NOT rebooted
#     (a leftover socket file from a crashed twin does not count — that boots fresh);
#   • the Studio server already running on :8080   → reused, nothing restarted.
# If you name a profile and a DIFFERENT build is running, it tells you how to switch
# (RESTART=1 ./qemu/studio.sh <profile>) instead of silently driving the wrong firmware.
#
# The server both serves the Studio page AND bridges it to the twin, so the browser's
# "Connect twin" is same-origin — no URL to figure out.
#   • GitHub Codespaces : open the auto-forwarded port 8080 (popup, or the Ports tab).
#   • Local VS Code     : open http://localhost:8080
#
# Then click "Connect twin" in the Studio. Ctrl-C stops the server; the twin keeps
# running (check it: ./qemu/twin-status.sh   stop it: ./qemu/twin-status.sh stop).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SERIAL_SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"
PORT="${BRIDGE_PORT:-8080}"
EXPLICIT=0; [ $# -ge 1 ] && EXPLICIT=1
PROFILE="${1:-vuln}"
# shellcheck source=twin-status.sh
. "$HERE/twin-status.sh"
WANT_IMG="$(twin_image_for "$PROFILE" "$HERE")"

# ── 1. the twin: reuse if running, otherwise boot ────────────────────────────
if twin_detect "$SERIAL_SOCK" 15; then
  if [ "${RESTART:-0}" = "1" ]; then
    echo "RESTART=1: stopping the running twin (pid $TWIN_PID, $TWIN_WHICH) and its Studio server…"
    twin_stop "$SERIAL_SOCK"
  elif [ "$EXPLICIT" = "1" ] && [ "$TWIN_IMG" != "$WANT_IMG" ]; then
    echo "A different build is already running: $TWIN_WHICH (pid $TWIN_PID)."
    echo "You asked for: $(_twin_profile_of "$WANT_IMG"). Switch with:   RESTART=1 ./qemu/studio.sh $PROFILE"
    exit 3
  else
    echo "twin already running — reusing it, not booting ($TWIN_WHICH, pid $TWIN_PID)."
  fi
elif [ "$TWIN_STATE" = "starting" ]; then
  echo "A twin (pid $TWIN_PID) is still booting; its console has not answered in 15 s."
  echo "Re-run in a moment, or check: ./qemu/twin-status.sh   (stop: ./qemu/twin-status.sh stop)"
  exit 3
fi

if ! twin_detect "$SERIAL_SOCK" 0; then
  echo "starting the twin ($PROFILE)…"
  SERIAL_SOCK="$SERIAL_SOCK" "$HERE/run.sh" "$PROFILE" >/tmp/twin.log 2>&1 &
  twin_detect "$SERIAL_SOCK" 30 || { echo "twin did not come up — see /tmp/twin.log"; exit 1; }
  echo "twin up ($TWIN_WHICH, pid $TWIN_PID)."
fi

# ── 2. the Studio server: reuse if already serving, otherwise start ─────────
echo "twin console   : $SERIAL_SOCK"
echo "Studio + bridge: http://localhost:$PORT   (Codespaces: open forwarded port $PORT)"
echo "text console   : ./qemu/console.sh        (attaches alongside the Studio)"
if pgrep -f "twin-bridge.py --port $PORT --serial $SERIAL_SOCK" >/dev/null 2>&1; then
  echo "Studio server already running on :$PORT — nothing to start. Open it and click 'Connect twin'."
  exit 0
fi
echo "In the Studio, click 'Connect twin'.  Ctrl-C stops the server (twin keeps running)."
exec python3 "$HERE/twin-bridge.py" --port "$PORT" --serial "$SERIAL_SOCK"
