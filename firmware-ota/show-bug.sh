#!/usr/bin/env bash
# show-bug.sh — find the unsigned-OTA bug in the firmware source (Part 6, Ex 3).
#
# Narrates each step so you learn WHILE it runs. This is the STATIC half of the lab:
# you proved the behavior against the QEMU twin (./qemu/run.sh) in Ex 2; now you find
# the line in the source that explains WHY. Physical hardware is reference only.
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type note >/dev/null 2>&1 || { step(){ printf '\n==== %s ====\n' "$*"; }; say(){ printf '» %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '→ %s\n' "$*"; }; predict(){ printf '? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '✎ FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }
set -euo pipefail

ROOT="$(cd "$HERE/.." && pwd)"; cd "$ROOT"
SRC="sbom/firmware/src"
[ -d "$SRC" ] || { echo "Firmware source not found at $SRC"; exit 1; }

step "Part 6 Ex 3 — locate the OTA 'verification' step in the firmware source"
say "Static meets dynamic: the runtime 'verified=NO' you saw must have a cause in the code."
predict "Before you read it: do you expect verify_firmware() to check a signature/hash, or to be a stub?"

say "Grep the OTA source for the verification symbols (the \\| alternation is easy to mistype)."
run bash -c "grep -rn 'verify_firmware\\|verified\\|Update.begin' '$SRC/' | head || true"
echo

observe "verify_firmware() returns a constant (true) — eth_ota.cpp:75-77: 'accept any image'."
observe "OBSERVED (static): the check is a no-op. That EXPLAINS the dynamic 'verified=NO'."
observe "What static can't tell you: that the endpoint is actually reachable at runtime (Ex 2 showed that)."
observe "What dynamic couldn't tell you: the exact code path — this grep did."
finding "Quote verify_firmware() with file:line; give the two-sentence static-vs-dynamic argument."
