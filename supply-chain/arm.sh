#!/usr/bin/env bash
# arm.sh — tell the mock update server to send the kill-switch trigger (Part 7).
#
# The compromised "update source" answers every beacon. This flips its answer
# from benign to the magic trigger token. It prints the exact command first.
#
# The command it runs:
#   echo ARM > /tmp/c2_state     drop the arming flag the C2 server checks on each beacon
set -euo pipefail
STATE="${C2_STATE:-/tmp/c2_state}"

echo "+ echo ARM > $STATE"
echo ARM > "$STATE"
echo
echo "C2 is now ARMED. The NEXT beacon returns the trigger —"
echo "watch the c2_server log flip benign -> ARMED, then check  ./qemu/relay.sh"
