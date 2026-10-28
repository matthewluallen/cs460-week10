#!/usr/bin/env bash
# benign.sh - reset the rogue time server to serve the real time (Part 8).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

STATE="${NTP_STATE:-/tmp/ntp_state}"

step "Reset the rogue SNTP server to benign (Part 8)"
say  "Return the server to answering with the real current time, so the next sync"
say  "sets a normal date and you can re-run the arm/observe cycle cleanly."

note "writing the benign flag (a redirect, shown rather than run-wrapped):"
note "\$ echo benign > $STATE"
echo benign > "$STATE"

observe "The server now serves the real time. Re-sync on the console ('ntpsync ...' or"
observe "wait for 'ntpsync auto'); 'clk' should read a current, un-truncated date again."
observe "Returning to benign does NOT 'repair' a wrapped clock by itself - the device"
observe "only recovers once it accepts a fresh in-range reply."
