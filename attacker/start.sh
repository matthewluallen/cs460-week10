#!/bin/sh
# Run both rogue servers bound to all interfaces, reading their benign/armed state
# from /state so the lab can flip them from the host (docker compose exec ...).
# Runs INSIDE the attacker container; tools/teach.sh usually isn't present here, so
# the printf fallback below does the narrating.
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

mkdir -p /state

step "Attacker container: launch both rogue servers"
say  "Bound to 0.0.0.0 so the twin can reach them at this container's IP ($(hostname -i))."
say  "Each server reads its benign/armed state from a /state file, so the host can flip"
say  "it live with ./attacker/arm.sh and ./attacker/benign.sh (docker compose exec ...)."
note "rogue NTP :1123 (udp+tcp) - Part 8    |    rogue C2/update :9000 (tcp) - Part 7"
note "arm NTP: echo arm > /state/ntp_state   (reset: echo benign > /state/ntp_state)"
note "arm C2 : echo ARM > /state/c2_state    (reset: echo benign > /state/c2_state)"
observe "Both run in the background; this script then waits on them (container stays up)."

# Backgrounded + env-prefixed, so launched directly (not via run, which runs foreground):
note "\$ python3 /app/ntp_server.py   (NTP_HOST=0.0.0.0 NTP_PORT=1123)"
NTP_HOST=0.0.0.0 NTP_PORT=1123 NTP_STATE=/state/ntp_state python3 /app/ntp_server.py &
note "\$ python3 /app/c2_server.py    (C2_HOST=0.0.0.0 C2_PORT=9000)"
C2_HOST=0.0.0.0  C2_PORT=9000  C2_STATE=/state/c2_state   python3 /app/c2_server.py &
wait
