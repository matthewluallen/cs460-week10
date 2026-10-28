#!/usr/bin/env bash
# Arm BOTH rogue servers in the attacker container (Parts 7 & 8).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

step "Arm both rogue servers in the attacker container (Parts 7 & 8)"
say  "Flip the container's shared state so NTP serves a post-2038 time (Part 8) and C2"
say  "serves the kill-switch trigger (Part 7). This writes /state inside the container."
predict "After the next 'ntpsync 172.30.0.10 1123', what year will 'clk' show?"

run docker compose -f "$HERE/docker-compose.yml" exec -T attacker sh -c 'echo arm > /state/ntp_state; echo ARM > /state/c2_state'

observe "attacker ARMED: NTP serves a post-2038 time; C2 serves the kill-switch trigger."
observe "On the console, 'ntpsync 172.30.0.10 1123' then 'clk' wraps to 1902-04-27 - the"
observe "same int32 truncation as Part 8, now driven from a distinct attacker IP."
finding "Record clk before/after and that the trigger came from 172.30.0.10, not loopback."
note "Reset with: ./attacker/benign.sh"
