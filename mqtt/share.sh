#!/usr/bin/env bash
# One command for the phone-sensor lab:
#   bash mqtt/share.sh
# It (1) starts the MQTT bridge if it isn't running, (2) makes the bridge port
# public so a phone with no GitHub login can reach it, and (3) prints a QR code
# right here in the terminal. Scan the QR with your phone to open the page.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
PORT="${PORT:-8090}"

# 1. deps + bridge
[ -d node_modules/qrcode ] || npm install --no-audit --no-fund >/dev/null 2>&1
if ! curl -sf -o /dev/null --max-time 3 "http://127.0.0.1:${PORT}/whoami" 2>/dev/null; then
  echo "Starting the phone-sensor bridge on :${PORT} ..."
  setsid bash -c "cd '$HERE' && node server.js > /tmp/phone-bridge.log 2>&1" </dev/null >/dev/null 2>&1 &
  for _ in $(seq 1 10); do
    curl -sf -o /dev/null --max-time 2 "http://127.0.0.1:${PORT}/whoami" 2>/dev/null && break
    sleep 1
  done
fi

# 2. public URL + make it public (Codespaces)
if [ -n "${CODESPACE_NAME:-}" ]; then
  URL="https://${CODESPACE_NAME}-${PORT}.app.github.dev/"
  if command -v gh >/dev/null 2>&1 && gh codespace ports visibility "${PORT}:public" -c "$CODESPACE_NAME" >/dev/null 2>&1; then
    PUB="public"
  else
    PUB="NOT-public"
  fi
else
  URL="http://localhost:${PORT}/"
  PUB="local"
fi

# 3. QR in the terminal
echo
node -e "require('./node_modules/qrcode').toString('${URL}',{type:'terminal',small:true},(e,s)=>{if(e){console.error(e.message);process.exit(1)}process.stdout.write(s)})"
echo
echo "  Scan the QR above with your phone, or open:"
echo "    ${URL}"
if [ "${PUB:-}" = "NOT-public" ]; then
  echo
  echo "  NOTE: couldn't confirm :${PORT} is public. If the phone hits a GitHub login page,"
  echo "        open the Ports panel, set ${PORT} -> Public (or Rebuild Container to automate it)."
fi
echo
