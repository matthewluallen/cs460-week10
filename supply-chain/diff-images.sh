#!/usr/bin/env bash
# diff-images.sh — compare the clean vs trojaned firmware images HONESTLY (Part 7, Ex 1).
#
# The lesson is the opposite of what you might expect: you CANNOT spot a small
# malicious change by diffing the deployed images. The two 8 MB flash images are the
# SAME size (padded), and a few added source lines relayout the whole app partition,
# so millions of bytes differ. The real payload is tiny — the compiled app binary
# grew ~624 bytes at BUILD time — but that number is a build fact, not something a
# diff of the shipped images reveals. This is why provenance (a reproducible
# reference build / signed attestation), not byte-diffing, is the real defense.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
A="qemu/twin/stamplc-flash-8MB.bin"
B="qemu/twin/stamplc-flash-8MB-MALICIOUS.bin"
[ -f "$A" ] && [ -f "$B" ] || { echo "Images missing — run ./qemu/fetch-qemu.sh"; exit 1; }

sa=$(wc -c < "$A"); sb=$(wc -c < "$B")
echo "+ ls -l $A $B"
ls -l "$A" "$B"
echo
echo "clean image     : $sa bytes"
echo "malicious image : $sb bytes"
echo "size delta      : $((sb - sa)) bytes   <-- a file-size check reveals NOTHING"
echo
echo "+ cmp -l $A $B | wc -l      (how many bytes actually differ)"
diffbytes=$( { cmp -l "$A" "$B" 2>/dev/null || true; } | wc -l | tr -d ' ')
echo "differing bytes : $diffbytes of $sa   <-- a raw diff is a haystack, not a needle"
echo
echo "The payload is only ~624 bytes of compiled code: at BUILD time the app binary"
echo "grew from 1,656,704 to 1,657,328 bytes. But adding those few lines relaid out"
echo "the entire app partition, so the shipped images differ in ~$diffbytes bytes."
echo "You cannot find the payload by diffing the deployed image — that is the point."
echo "Establishing trust needs PROVENANCE (a reproducible reference build or a signed"
echo "attestation of the exact source + deps), not a byte-diff."
