#!/usr/bin/env bash
# benign.sh — reset the mock update server to its normal "you're up to date" reply (Part 7).
#
# Narrates the reset. Undoes arm.sh by removing the arming flag. Note this only changes
# what the SERVER sends; a switch that already fired stays latched in the running image
# until you reboot the twin. Everything runs on 127.0.0.1; hardware is reference only.
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type note >/dev/null 2>&1 || { step(){ printf '\n==== %s ====\n' "$*"; }; say(){ printf '» %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '→ %s\n' "$*"; }; predict(){ printf '? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '✎ FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }
set -euo pipefail
STATE="${C2_STATE:-/tmp/c2_state}"

step "Part 7 — reset the update source to its benign reply"
say "Return c2_server.py to 'you're up to date' by removing the arming flag."
run rm -f "$STATE"
observe "Server now answers benign again."
observe "OBSERVED: the server reply changed. NOT changed: a relay0 that already fired — the"
observe "switch stays LATCHED in the running image until you reboot the twin."
finding "To re-run the detonation cleanly, stop the twin and ./supply-chain/run-malicious.sh again."
