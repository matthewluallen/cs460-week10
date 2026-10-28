#!/usr/bin/env bash
# decode.sh — decode the recorded Modbus RTU serial exchanges with the pure-Node CLI.
# No device, network or Python: runs entirely in your Codespace. Narrates each step.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"            # bus-analysis/
source "$HERE/../tools/teach.sh" 2>/dev/null || true
# Backfill any narration helper teach.sh did not provide (missing or partial), so the
# script still runs and teaches even without it.
declare -F step    >/dev/null 2>&1 || step()    { printf '\n==== %s ====\n' "$*"; }
declare -F say     >/dev/null 2>&1 || say()     { printf '>  %s\n' "$*"; }
declare -F run     >/dev/null 2>&1 || run()     { printf '   $ %s\n' "$*"; "$@"; }
declare -F observe >/dev/null 2>&1 || observe() { printf '-> %s\n' "$*"; }
declare -F predict >/dev/null 2>&1 || predict() { printf '?  PREDICT FIRST: %s\n' "$*"; }
declare -F finding >/dev/null 2>&1 || finding() { printf 'FINDING: %s\n' "$*"; }
declare -F note    >/dev/null 2>&1 || note()    { printf '   %s\n' "$*"; }

step "Modbus RTU decode — recorded serial bytes (offline, no device)"
say "These are retained bytes from an owned device, decoded locally. Nothing connects out."
predict "Write down: what register 25 decodes to + its likely meaning, and WHY two rereads of the identical request might differ."

step "1/4 — the request"
say "Decode the function-03 read so you know exactly what was asked for before trusting any reply."
run node "$HERE/tools/decode.js" rtu-request "$HERE/serial/physical-0-request.bin"
observe "Expect status=valid, function 03, start register 25, count 1. This fixes the QUESTION the replies answer."

step "2/4 — reply physical-0"
say "Decode the first complete reply to this request."
run node "$HERE/tools/decode.js" rtu-reply "$HERE/serial/physical-0-reply.bin" "$HERE/serial/physical-0-request.bin"
observe "Read register 25's value. status=valid means well-formed + CRC-matched; it does NOT tell you the units or that the number is process truth."

step "3/4 — reply physical-3 (SAME request)"
say "Decode a second reply to the byte-for-byte identical request, to compare acquisitions."
run node "$HERE/tools/decode.js" rtu-reply "$HERE/serial/physical-3-reply.bin" "$HERE/serial/physical-3-request.bin"
observe "Register 25 differs from step 2 (96 vs 99), both CRC-valid: a reread is a NEW acquisition of a live quantity, not corruption of the first."

step "4/4 — the empty timeout reply"
say "Decode the earlier run's empty reply; an absent reply is not a zero."
note "This case is EXPECTED to exit nonzero — that is correct evidence handling, not a broken install."
set +e
run node "$HERE/tools/decode.js" rtu-reply "$HERE/serial/timeout-reply.bin" "$HERE/serial/timeout-request.bin"
TIMEOUT_RC=$?
set -e
observe "status=unavailable, no accepted value, exit code $TIMEOUT_RC (nonzero expected)."

finding "Record register 25's two decoded values, that both frames are matched + CRC-valid, and that the difference is acquisition timing (fresh sample) — NOT corruption, and NOT proof of units/scale (the firmware map is still required). Open i2c-analyzer.html for the same fields beside the bytes."
