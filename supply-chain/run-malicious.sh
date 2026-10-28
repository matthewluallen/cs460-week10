#!/usr/bin/env bash
# run-malicious.sh — boot the TROJANED firmware image in the twin (Part 7).
#
# Narrates the launch, then hands the terminal to the twin. Same QEMU twin as Part 6
# (./qemu/run.sh), just a different flash image and a full-speed clock. Everything
# runs on 127.0.0.1; physical hardware is reference/validation only.
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type note >/dev/null 2>&1 || { step(){ printf '\n==== %s ====\n' "$*"; }; say(){ printf '» %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '→ %s\n' "$*"; }; predict(){ printf '? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '✎ FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }
set -euo pipefail

ROOT="$(cd "$HERE/.." && pwd)"; cd "$ROOT"
IMG="${FLASH_IMG:-qemu/twin/stamplc-flash-8MB-MALICIOUS.bin}"
[ -f "$IMG" ] || { echo "Malicious image not found: $IMG   (run ./qemu/fetch-qemu.sh)"; exit 1; }

step "Part 7 Ex 2 — boot the trojaned image in the twin"
say "Attacker's intent: the device runs an image that LOOKS legitimate but carries a dormant"
say "beacon + relay-force. We boot it to watch the switch arm over an outbound 'update check.'"
say "ICOUNT=off runs the virtual clock at host speed (fast beacon loop); FLASH_IMG picks the trojaned image."
predict "Before it boots: will the switch fire on its own, or wait for a signal from the update server?"
observe "It stays dormant until the C2 reply flips to the trigger — detonation is in arm.sh, next."
finding "Note that nothing you did INBOUND started this; the device beacons OUTBOUND on its own."
note "Latched relay0 persists until you reboot the twin — stop this and re-run to reset cleanly."
note "\$ ICOUNT=off FLASH_IMG=$IMG ./qemu/run.sh   # handing the terminal to the twin now"
exec env ICOUNT=off FLASH_IMG="$IMG" ./qemu/run.sh
