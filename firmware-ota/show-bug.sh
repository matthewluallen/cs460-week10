#!/usr/bin/env bash
# show-bug.sh — find the unsigned-OTA bug in the firmware source (Part 6, Ex 3).
#
# The regex escaping (\| alternation) is exactly the kind of thing that's easy to
# fumble, so this wraps it. It prints the command it runs first.
#
# The command it runs, part by part:
#   grep -r    search recursively through files
#   -n         show line numbers (so you can quote file:line in your finding)
#   'A\|B\|C'  match any of these strings (\| means OR in basic regex)
#   sbom/firmware/src/   where the firmware source lives
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"

SRC="sbom/firmware/src"
[ -d "$SRC" ] || { echo "Firmware source not found at $SRC"; exit 1; }

echo "+ grep -rn 'verify_firmware\\|verified\\|Update.begin' $SRC/"
echo
grep -rn "verify_firmware\|verified\|Update.begin" "$SRC/" | head || true
echo
echo "Look at verify_firmware(): it returns a constant — it never checks a"
echo "signature or hash. That single line is the vulnerability; the runtime"
echo "'verified=NO' you saw in Exercise 2 is its consequence."
