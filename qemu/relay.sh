#!/usr/bin/env bash
# relay.sh — read one relay/coil's state from the PLC's HMI JSON (Parts 6-7).
#
# The grep quoting here is fiddly to type by hand, so this wraps it. It prints the
# exact command before running it.
#
#   ./qemu/relay.sh            # show relay0  (the kill-switch target in Part 7)
#   ./qemu/relay.sh relay2     # show a different point
#
# The command it runs, part by part:
#   curl -s http://127.0.0.1:8080/api/points   fetch the HMI's live point list (JSON)
#   grep -o '"n":"relay0"[^}]*'                 keep ONLY that point's object
#                                               ("v":1 = energised, "v":0 = off)
set -euo pipefail
NAME="${1:-relay0}"
URL="${HMI_URL:-http://127.0.0.1:8080/api/points}"

echo "+ curl -s $URL | grep -o '\"n\":\"$NAME\"[^}]*'"
echo
curl -s "$URL" | grep -o "\"n\":\"$NAME\"[^}]*" \
  || echo "(no point named $NAME in the response — is the twin running?)"
