#!/usr/bin/env bash
# arm.sh - tell the rogue time server to serve a post-2038 time (Part 8).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

STATE="${NTP_STATE:-/tmp/ntp_state}"

step "Arm the rogue SNTP server (Part 8)"
say  "Flip the time server from benign to armed. Once armed it answers every SNTP"
say  "query with a time past 2038-01-19 03:14:07 UTC - the signed-32-bit boundary."
say  "Nothing connects TO the device: the trigger rides its own outbound time query."
predict "Before the PLC re-syncs, write down the exact year 'clk' will show. The wire"
predict "value is 2038-06-03; the firmware stores it in a signed int32. 1901? 1902?"

note "writing the armed flag (a redirect, so shown here rather than run-wrapped):"
note "\$ echo arm > $STATE"
echo arm > "$STATE"

observe "The ntp_server log will flip 'benign -> ARMED' on its next query. Then run"
observe "'clk' on the plc> console: it reads 1902-04-27 17:31:44 UTC (the int32 wrap of"
observe "the 2038-06-03 epoch, -2135831296), flagged 'WRAPPED! 32-bit overflow'."
observe "This establishes unauth time + a 32-bit store can rewrite the clock; it does NOT"
observe "show the audit log was corrupted - check 'log' next (its timestamps use millis())."
finding "clk before/after the arm, the ntp_server ARMED line, and that 'log' timestamps"
finding "stayed monotonic (millis()) while clk read 1902 - the wrap did not move them."
note "Reset with: ./ntp-aitm/benign.sh"
