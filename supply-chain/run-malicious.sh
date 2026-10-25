#!/usr/bin/env bash
# run-malicious.sh — boot the TROJANED firmware image in the twin (Part 7).
#
# It's the same twin as Part 6, just a different flash image and a full-speed
# clock. This wraps the env-prefixed launch so you don't type it; it prints the
# exact command first.
#
# The command it runs, part by part:
#   ICOUNT=off                              run the virtual clock at host speed
#                                           (fast — good for the beacon/upload loop)
#   FLASH_IMG=qemu/twin/...-MALICIOUS.bin    boot the malicious 8 MB image
#                                           instead of the clean one
#   ./qemu/run.sh                           the very same launcher Part 6 used
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"

IMG="${FLASH_IMG:-qemu/twin/stamplc-flash-8MB-MALICIOUS.bin}"
[ -f "$IMG" ] || { echo "Malicious image not found: $IMG   (run ./qemu/fetch-qemu.sh)"; exit 1; }

echo "+ ICOUNT=off FLASH_IMG=$IMG ./qemu/run.sh"
echo
exec env ICOUNT=off FLASH_IMG="$IMG" ./qemu/run.sh
