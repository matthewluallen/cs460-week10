#!/usr/bin/env bash
# ota-push.sh — push a firmware image to the StampLC's OTA endpoint (Part 6).
#
# This wraps ONE curl command so you don't have to type it. Everything it does is
# spelled out below, and it prints the exact command before running it — so you
# SEE the full command, you just don't have to retype it.
#
#   ./qemu/ota-push.sh                 # push a tiny UNSIGNED demo payload
#   ./qemu/ota-push.sh my-image.bin    # push a file you choose
#
# The command it runs, part by part:
#   curl                             the HTTP client
#   -s                               silent — hide the progress meter
#   -X POST                          use POST: we are UPLOADING, not fetching
#   --data-binary @FILE              send FILE's raw bytes verbatim as the body
#                                    (no form-encoding, no filename munging)
#   http://127.0.0.1:80/api/upload   the firmware-update endpoint on the twin
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"

URL="${OTA_URL:-http://127.0.0.1:80/api/upload}"
FILE="${1:-}"

if [ -z "$FILE" ]; then
  FILE="$(mktemp /tmp/unsigned-demo.XXXXXX.bin)"
  printf 'MALICIOUS-UNSIGNED-OTA' > "$FILE"
  echo "No file given — wrote a $(wc -c < "$FILE")-byte UNSIGNED demo payload to $FILE"
  echo
fi
[ -f "$FILE" ] || { echo "File not found: $FILE"; exit 1; }

# Show the student the exact command, then run it.
echo "+ curl -s -X POST --data-binary @$FILE $URL"
echo
curl -s -X POST --data-binary @"$FILE" "$URL"; echo
