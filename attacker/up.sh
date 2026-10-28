#!/usr/bin/env bash
# up.sh — start the attacker container (lab-attacker at 172.30.0.10). Wraps the one
# long `docker compose ... up` line so you run it as a short command.
#
#   ./attacker/up.sh        # build + start both rogue servers (NTP :1123, C2 :9000)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

step "Start the attacker host (optional realistic setup)"
say  "By default the rogue servers run on 127.0.0.1 - the attacker shares the device's"
say  "own loopback. This brings them up in a SEPARATE container at a distinct IP"
say  "(172.30.0.10) so the twin sees the attacker as its own host on the network."

run docker compose -f "$HERE/docker-compose.yml" up -d --build

observe "lab-attacker is up at 172.30.0.10: rogue NTP :1123 (Part 8) and rogue C2/update"
observe ":9000 (Part 7). The twin's W5500 bridge lets the guest reach any host-routable"
observe "address, so 'ntpsync 172.30.0.10 1123' on the console targets this container."
note "Arm/reset with:  ./attacker/arm.sh   ./attacker/benign.sh    Stop with: ./attacker/down.sh"
