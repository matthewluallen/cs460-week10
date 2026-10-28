#!/usr/bin/env bash
# diff-images.sh — compare the clean vs trojaned firmware images HONESTLY (Part 7, Ex 1).
#
# Narrates the lesson AS it runs: you CANNOT spot a small malicious change by diffing
# the deployed images. Both 8 MB flash images are the SAME size, and a few added source
# lines relayout the whole app partition, so millions of bytes differ — while the real
# payload is ~624 bytes (a BUILD fact, invisible in the shipped image). This is why
# provenance beats byte-diffing. Runs on local files; no twin needed for this step.
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type note >/dev/null 2>&1 || { step(){ printf '\n==== %s ====\n' "$*"; }; say(){ printf '» %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '→ %s\n' "$*"; }; predict(){ printf '? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '✎ FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }
set -euo pipefail

ROOT="$(cd "$HERE/.." && pwd)"; cd "$ROOT"
A="qemu/twin/stamplc-flash-8MB.bin"
B="qemu/twin/stamplc-flash-8MB-MALICIOUS.bin"
[ -f "$A" ] && [ -f "$B" ] || { echo "Images missing — run ./qemu/fetch-qemu.sh"; exit 1; }

step "Part 7 Ex 1 — diff the clean vs trojaned flash images, honestly"
say "Attacker's intent: the smallest, quietest change that works. The compiled payload is ~624 bytes."
predict "Of the 8,388,608 bytes per image, how many differ — hundreds, or millions? Can a no-reference diff pin the 624-byte payload?"

say "First, compare file sizes."
run ls -l "$A" "$B"
sa=$(wc -c < "$A"); sb=$(wc -c < "$B")
observe "clean=$sa bytes, malicious=$sb bytes, size delta=$((sb - sa)) — a file-size check reveals NOTHING."

say "Now count how many bytes actually differ (cmp -l lists them, wc -l counts)."
run bash -c "{ cmp -l '$A' '$B' 2>/dev/null || true; } | wc -l | tr -d ' '"
echo

observe "That differing-byte count is MILLIONS, not hundreds: adding a few source lines relaid out"
observe "the whole app partition. The ~624-byte payload (app binary grew 1,656,704 -> 1,657,328 at"
observe "BUILD time) is NOT recoverable from this diff of the shipped image."
observe "OBSERVED: same size, millions differ. REFUTED inference: 'a byte-diff would reveal the payload.'"
finding "Establishing trust needs PROVENANCE (reproducible reference build / signed attestation), not a byte-diff."
