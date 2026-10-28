#!/usr/bin/env bash
# show-bug.sh - point at the Year-2038 truncation in the network time client (Part 8).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

ROOT="$(cd "$HERE/.." && pwd)"; cd "$ROOT"

step "Static view: where the Y2038 bug lives (Part 8)"
say  "The runtime wrap you saw with 'clk' has one cause in source. Grep the deciding"
say  "lines in the network time client so you can tie the observation to the code."
predict "Before you read it: is the stored clock field 32-bit or 64-bit? signed?"

run grep -nE "int32_t +s_epoch32|\(int32_t\)unix64|settimeofday" sbom/firmware/src/time_sync.cpp

observe "s_epoch32 is a SIGNED 32-bit field (CWE-197). The 64-bit network time is cast"
observe "into it, so any value past 2038-01-19 03:14:07 UTC wraps negative and the clock"
observe "reads 1902 (the armed 2038-06-03 epoch truncates to -2135831296 ->"
observe "1902-04-27 17:31:44 UTC). A correct controller keeps time in 64-bit time_t and"
observe "uses a MONOTONIC clock for timers/logs - which is why the audit log survives."
observe "What the code shows (a cast) the runtime wrap does not; what the runtime shows"
observe "(the exact 1902 date + the surviving log) the cast alone does not. Report both."
finding "Quote the truncation line and one sentence on static-vs-dynamic: what the code"
finding "read tells you that the runtime wrap doesn't, and vice-versa."
