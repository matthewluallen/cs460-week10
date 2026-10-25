#!/usr/bin/env bash
# benign.sh - reset the rogue time server to serve the real time (Part 8).
set -euo pipefail
STATE="${NTP_STATE:-/tmp/ntp_state}"
echo "+ echo benign > $STATE"
echo benign > "$STATE"
echo "benign. The server now answers with the real current time again."
