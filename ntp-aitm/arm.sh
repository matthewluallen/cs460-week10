#!/usr/bin/env bash
# arm.sh - tell the rogue time server to serve a post-2038 time (Part 8).
set -euo pipefail
STATE="${NTP_STATE:-/tmp/ntp_state}"
echo "+ echo arm > $STATE"
echo arm > "$STATE"
echo
echo "ARMED. The next time the PLC syncs, the server returns a time past 2038-01-19;"
echo "the 32-bit clock wraps. Watch the ntp_server log, then check  'clk'  on the PLC."
