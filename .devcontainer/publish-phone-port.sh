#!/usr/bin/env bash
# Make the phone-sensor bridge port PUBLIC so a phone (with no GitHub login) can
# scan the QR on the bridge page and open it. Runs automatically on Codespace
# start (postStartCommand) and can be re-run any time:  bash .devcontainer/publish-phone-port.sh [port]
#
# Safe no-op outside Codespaces, without gh, or if org policy forbids public ports.
# The port must be in devcontainer.json "forwardPorts" for a tunnel to exist —
# gh can only change an existing forward's visibility, it cannot create one.
set -u
PORT="${1:-8090}"

if [ -z "${CODESPACE_NAME:-}" ]; then
  echo "[ports] not running in a Codespace — nothing to publish (open http://localhost:${PORT} locally)."
  exit 0
fi
if ! command -v gh >/dev/null 2>&1; then
  echo "[ports] gh CLI not found — cannot set port visibility."
  exit 0
fi

URL="https://${CODESPACE_NAME}-${PORT}.app.github.dev"
# The forwardPorts tunnel can take a few seconds to register after start; retry.
for _ in $(seq 1 20); do
  if gh codespace ports visibility "${PORT}:public" -c "$CODESPACE_NAME" >/dev/null 2>&1; then
    echo "[ports] port ${PORT} is PUBLIC  ->  ${URL}"
    echo "[ports] start the bridge (node mqtt/server.js) and scan the QR on that page from a phone."
    exit 0
  fi
  sleep 3
done
echo "[ports] could not make ${PORT} public automatically (tunnel not ready, or org policy blocks public ports)."
echo "[ports] Manual fallback: VS Code 'Ports' panel -> right-click ${PORT} -> Port Visibility -> Public."
echo "[ports] Once public it will be at: ${URL}"
exit 0
