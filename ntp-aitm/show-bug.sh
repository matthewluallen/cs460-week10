#!/usr/bin/env bash
# show-bug.sh - point at the Year-2038 truncation in the network time client (Part 8).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
echo "+ grep -nE 'int32_t +s_epoch32|\(int32_t\)unix64|settimeofday' sbom/firmware/src/time_sync.cpp"
echo
grep -nE "int32_t +s_epoch32|\(int32_t\)unix64|settimeofday" sbom/firmware/src/time_sync.cpp
echo
echo "s_epoch32 is a SIGNED 32-bit field (CWE-197). The 64-bit network time is cast into"
echo "it, so any value past 2038-01-19 03:14:07 UTC wraps negative and the clock reads 1902."
echo "A correct controller keeps time in 64-bit time_t (and uses a monotonic clock for timers)."
