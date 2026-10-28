#!/usr/bin/env bash
# show-relay.sh — read a relay/coil's state from the PLC HMI JSON (Part 7, Ex 2).
#
# Narrates WHY you're reading the relay, then delegates to the canonical qemu/relay.sh
# (one source of truth for the curl|grep readback). Reads the twin's HMI on bridged
# port 8080 at 127.0.0.1; physical hardware is reference/validation only.
#
#   ./supply-chain/show-relay.sh            # show relay0 (the kill-switch target)
#   ./supply-chain/show-relay.sh relay2     # show a different point
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type note >/dev/null 2>&1 || { step(){ printf '\n==== %s ====\n' "$*"; }; say(){ printf '» %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '→ %s\n' "$*"; }; predict(){ printf '? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '✎ FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }
set -euo pipefail
NAME="${1:-relay0}"

step "Part 7 Ex 2 — read '$NAME' back from the HMI point list"
say "This is the PHYSICAL effect readout: did the kill switch move the coil?"
say "'v':1 = energized (switch fired), 'v':0 = off. Read it BEFORE and AFTER you arm the C2."
predict "Right now (before arming): do you expect '$NAME' to read 0 or 1?"
observe "The value you see is the OBSERVATION. 'An operator would see a pump energize' is the INFERENCE."
finding "Record '$NAME' before and after arming — that before/after pair is your detonation evidence."
note "\$ delegating to ./qemu/relay.sh $NAME"
exec "$HERE/../qemu/relay.sh" "$NAME"
