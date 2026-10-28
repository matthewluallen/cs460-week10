#!/usr/bin/env bash
# Reset BOTH rogue servers to benign.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

step "Reset both rogue servers to benign (Parts 7 & 8)"
say  "Return the container's NTP to the real time and clear the C2 trigger, so you can"
say  "re-run the arm/observe cycle from a clean baseline."

run docker compose -f "$HERE/docker-compose.yml" exec -T attacker sh -c 'echo benign > /state/ntp_state; echo benign > /state/c2_state'

observe "attacker reset to benign: NTP serves the real time; C2 no longer triggers."
observe "Re-sync on the console to recover a current date; a benign server does not by"
observe "itself un-wrap a clock the device already latched."
