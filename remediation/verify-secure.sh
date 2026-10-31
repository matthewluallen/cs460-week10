#!/usr/bin/env bash
# verify-secure.sh — re-run the Part 6-8 attacks against a running SECURE twin and
# show each one now fails (Part 9). Behavioral proof of the hardening, not a grep.
#
#   ./qemu/build-twin.sh secure                         # compile + package the hardened 8MB image
#   ./qemu/run.sh secure                                # boot it in the twin, leave it running
#   ./remediation/verify-secure.sh                      # <- then run this (another shell)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"
[ -S "$SOCK" ] || { echo "No serial socket at $SOCK — boot the SECURE twin first:  ./qemu/run.sh secure"; exit 1; }

step "Re-run the Part 6-8 attacks on the SECURE build (Part 9)"
say  "This drives the LIVE console and HTTP services (it does not read a banner or grep"
say  "source). For each earlier-part attack it checks the EFFECT is gone, on this image"
say  "with this project. The twin is the lab environment; the bench is validation only."
note "Attacks it replays and the control that stops each:"
note "  Part 6/7  unauth OTA upload       -> 401   (architecture: auth + HMAC now required)"
note "  console   admin:openplc backdoor  -> refused (line-of-code: the branch was removed)"
note "  console   default operator:1234   -> refused (architecture: no default ships)"
note "  console   5 bad logins            -> lockout (architecture: new lockout state machine)"
note "  Part 8    NTP post-2038 step      -> clk does NOT wrap (line-of-code: int32 -> int64 store)"
note "  Part 9    /api/tag overflow       -> relay0 stays OFF (line-of-code: bounds-checked copy)"
predict "For each attack, write down (a) will it still work, and (b) is it stopped by a"
predict "SINGLE LINE or by a DESIGN change? Compare your two columns to the PASS/FAIL below."
observe "A PASS = that path is closed on THIS build + project. It does NOT prove the"
observe "transport is encrypted, the HMAC key is secret (it's a repo constant), or that"
observe "every other route is safe. Note any attack you expected to fail that still worked."
finding "For your finding: what failed in the vulnerable baseline, what changed here, which"
finding "positive control still passed, and what remains possible."
echo

note "running the behavioral harness:"
run python3 "$HERE/verify-secure.py"
