#!/usr/bin/env bash
# console.sh — attach to the twin's live plc> serial console (Parts 6-7).
#
# One short command instead of remembering the socket path and the nc flags.
#
# The command it runs, part by part:
#   nc            netcat — a raw connection tool
#   -U            connect to a UNIX-domain socket (a local pipe), not a TCP port
#   /tmp/plc-serial   the socket run.sh exposes the PLC's UART on
#
# To QUIT the console:  press  Ctrl-C        (or just close the terminal pane)
set -euo pipefail
SOCK="${SERIAL_SOCK:-/tmp/plc-serial}"
[ -S "$SOCK" ] || { echo "No serial socket at $SOCK — is the twin running?  (./qemu/run.sh)"; exit 1; }

echo "+ nc -U $SOCK"
echo "  (quit the console with:  Ctrl-C , or close the pane)"
echo
exec nc -U "$SOCK"
