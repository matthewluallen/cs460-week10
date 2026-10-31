#!/usr/bin/env bash
# twin-status.sh — is a twin already running? (and, if asked, stop it)
#
#   ./qemu/twin-status.sh            # report: running / starting / stopped (exit 0 / 0 / 1)
#   ./qemu/twin-status.sh stop       # stop the twin (and the Studio bridge) on this socket
#
# run.sh and studio.sh source this file and call twin_detect before they boot anything,
# so starting the lab twice never boots a second twin over the first one.
#
# "Already running" means BOTH of these hold, not just that a file exists:
#   1. a qemu-system-xtensa process is alive whose console is THIS socket
#      (its command line contains  unix:$SERIAL_SOCK,server ), and
#   2. that socket accepts a connection (a socket file left behind by a crashed twin
#      refuses, so it is treated as stale and the twin is booted fresh).
# The image path on that process's command line tells you WHICH build is running.

_twin_pids() {        # pids of twins serving socket $1
  pgrep -f "qemu-system-xtensa.*unix:$1,server" 2>/dev/null || true
}

_twin_img_of() {      # flash image path on pid $1's command line
  ps -o args= -p "$1" 2>/dev/null | sed -n 's/.*-drive file=\([^,]*\),if=mtd.*/\1/p'
}

_twin_sock_live() {   # 0 if socket $1 accepts a connection
  [ -S "$1" ] || return 1
  python3 - "$1" <<'PY' 2>/dev/null
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(1.5)
try:
    s.connect(sys.argv[1])
except OSError:
    sys.exit(1)
s.close()
PY
}

_twin_profile_of() {  # human name for an image path
  case "$(basename "${1:-}")" in
    stamplc-flash-8MB.bin)           echo "vulnerable teaching build" ;;
    stamplc-secure-8MB.bin)          echo "hardened secure build" ;;
    stamplc-flash-8MB-MALICIOUS.bin) echo "supply-chain malicious image" ;;
    "")                              echo "unknown build" ;;
    *)                               echo "custom image $(basename "$1")" ;;
  esac
}

# twin_image_for PROFILE QEMU_DIR — the image run.sh boots for a profile name
twin_image_for() {
  case "${1:-vuln}" in
    secure|hardened) echo "$2/twin/stamplc-secure-8MB.bin" ;;
    malicious|evil)  echo "$2/twin/stamplc-flash-8MB-MALICIOUS.bin" ;;
    *)               echo "$2/twin/stamplc-flash-8MB.bin" ;;
  esac
}

# twin_detect SOCK [WAIT_SECONDS]
#   sets TWIN_STATE = running | starting | stale | stopped,  TWIN_PID, TWIN_IMG, TWIN_WHICH
#   returns 0 when a live twin is serving SOCK (after waiting up to WAIT_SECONDS if it is
#   still booting), 1 otherwise.
twin_detect() {
  local sock="$1" wait="${2:-15}" pid
  TWIN_PID=""; TWIN_IMG=""; TWIN_WHICH=""; TWIN_STATE="stopped"
  pid="$(_twin_pids "$sock" | head -n1)"
  if [ -n "$pid" ]; then
    TWIN_PID="$pid"; TWIN_IMG="$(_twin_img_of "$pid")"; TWIN_WHICH="$(_twin_profile_of "$TWIN_IMG")"
    local i=0
    while :; do
      if _twin_sock_live "$sock"; then TWIN_STATE="running"; return 0; fi
      kill -0 "$pid" 2>/dev/null || { TWIN_STATE="stopped"; TWIN_PID=""; break; }
      [ "$i" -ge $((wait * 5)) ] && { TWIN_STATE="starting"; return 1; }
      i=$((i + 1)); sleep 0.2
    done
  fi
  [ -e "$sock" ] && TWIN_STATE="stale"
  return 1
}

# twin_stop SOCK [bridge] — stop the twin on SOCK; with "bridge", also stop the Studio server
# attached to it. (A restart leaves the server up: it reconnects to the new twin by itself,
# so an open Studio tab keeps working.)
twin_stop() {
  local sock="$1" pids
  pids="$(_twin_pids "$sock")"
  [ "${2:-}" = "bridge" ] && { pkill -f "twin-bridge.py.*--serial $sock" 2>/dev/null || true; }
  if [ -n "$pids" ]; then
    # shellcheck disable=SC2086
    kill $pids 2>/dev/null || true
    for _ in $(seq 1 50); do [ -z "$(_twin_pids "$sock")" ] && break; sleep 0.1; done
  fi
  rm -f "$sock"
}

# ── run directly ──────────────────────────────────────────────────────────────
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  set -uo pipefail
  SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"
  case "${1:-status}" in
    status)
      if twin_detect "$SOCK" 0; then
        echo "twin: RUNNING  pid $TWIN_PID  $TWIN_WHICH  ($TWIN_IMG)"
        echo "      console socket $SOCK accepts connections — nothing to boot."
        pgrep -f "twin-bridge.py.*--serial $SOCK" >/dev/null 2>&1 \
          && echo "      Studio bridge is up (http://localhost:${BRIDGE_PORT:-8080}); text console: ./qemu/console.sh" \
          || echo "      no Studio bridge yet — ./qemu/studio.sh adds it without rebooting the twin"
        exit 0
      fi
      case "$TWIN_STATE" in
        starting) echo "twin: STARTING  pid $TWIN_PID  $TWIN_WHICH — console not ready yet"; exit 0 ;;
        stale)    echo "twin: STOPPED (stale socket $SOCK left by an earlier twin; the next boot clears it)"; exit 1 ;;
        *)        echo "twin: STOPPED — boot it with ./qemu/studio.sh (or ./qemu/run.sh)"; exit 1 ;;
      esac ;;
    stop)
      twin_detect "$SOCK" 0 >/dev/null 2>&1 || true
      twin_stop "$SOCK" bridge
      echo "twin on $SOCK stopped${TWIN_WHICH:+ ($TWIN_WHICH)}, and its Studio server if one was running." ;;
    *) echo "usage: ./qemu/twin-status.sh [status|stop]"; exit 2 ;;
  esac
fi
