#!/usr/bin/env bash
# benign.sh — reset the mock update server to its normal "you're up to date" reply.
#
# Undoes arm.sh. Prints the exact command first.
#
# The command it runs:
#   rm -f /tmp/c2_state      remove the arming flag; the C2 server answers benign again
set -euo pipefail
STATE="${C2_STATE:-/tmp/c2_state}"

echo "+ rm -f $STATE"
rm -f "$STATE"
echo
echo "C2 is back to BENIGN (the switch stays latched in the running image until you reboot the twin)."
