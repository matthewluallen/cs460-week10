#!/usr/bin/env bash
# arm.sh — tell the mock update server to send the kill-switch trigger (Part 7).
#
# Narrates the attacker move. The compromised "update source" (c2_server.py on :9000)
# answers every outbound beacon from the twin; this flips its answer to the trigger.
# Everything runs on 127.0.0.1; physical hardware is reference/validation only.
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type note >/dev/null 2>&1 || { step(){ printf '\n==== %s ====\n' "$*"; }; say(){ printf '» %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '→ %s\n' "$*"; }; predict(){ printf '? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '✎ FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }
set -euo pipefail
STATE="${C2_STATE:-/tmp/c2_state}"

step "Part 7 Ex 2 — ARM the kill switch at the (compromised) update source"
say "Attacker's intent: you don't touch the device. You change what its UPDATE SERVER replies."
say "Dropping the arming flag makes c2_server.py answer the next beacon with the magic trigger."
predict "After the NEXT beacon, will relay0 flip to 1? And will any INBOUND connection to the device be needed?"
note "\$ echo ARM > $STATE   # (redirected write, shown not run-wrapped)"
echo ARM > "$STATE"
observe "Flag set. The device pulls the trigger via its own OUTBOUND update check — nothing inbound."
observe "OBSERVED: C2 log flips benign -> ARMED, then relay0 energizes. INFERRED: a real plant actuates."
finding "Capture the C2 log (benign -> ARMED) and relay0 before/after (./supply-chain/show-relay.sh)."
finding "Graded sentence: \"nothing inbound; it rode the outbound update check.\""
