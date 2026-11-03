#!/usr/bin/env bash
# One command for the phone-sensor lab:
#   bash mqtt/share.sh            # Codespaces: uses the auto-forwarded public port
#   bash mqtt/share.sh --tunnel   # local Dev Container: opt-in public tunnel for your phone
# It (1) starts the MQTT bridge if it isn't running, (2) gets a URL a phone with no
# GitHub login can reach — the Codespaces forwarded port, or (with --tunnel) a free
# Cloudflare quick tunnel — and (3) prints a QR code right here in the terminal.
# Scan the QR with your phone to open the page.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
PORT="${PORT:-8090}"

# A phone can't reach "localhost" on the laptop, so for a LOCAL Dev Container we offer an
# OPT-IN public tunnel: Cloudflare's free, no-account "quick tunnel" (the trial edition).
# It is NEVER opened automatically — the student passes --tunnel (or TUNNEL=1), and we
# print a clear heads-up that a temporary PUBLIC url is created and that the phone's
# readings travel through Cloudflare. In Codespaces the forwarded port is used instead.
TUNNEL="${TUNNEL:-0}"
for _a in "$@"; do case "$_a" in --tunnel) TUNNEL=1 ;; --no-tunnel) TUNNEL=0 ;; esac; done
TURL=""

_ensure_cloudflared() {            # echoes the cloudflared path on stdout, or returns 1
  local cf arch a
  cf="$(command -v cloudflared 2>/dev/null || true)"
  if [ -n "$cf" ]; then echo "$cf"; return 0; fi
  arch="$(uname -m)"
  case "$arch" in x86_64|amd64) a=amd64 ;; aarch64|arm64) a=arm64 ;; *) return 1 ;; esac
  cf="$HERE/.cache/cloudflared"
  if [ ! -x "$cf" ]; then
    mkdir -p "$HERE/.cache"
    echo "  Downloading the free Cloudflare tunnel client (cloudflared-${a})..." >&2
    curl -fsSL "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-${a}" -o "$cf" 2>/dev/null || return 1
    chmod +x "$cf"
  fi
  echo "$cf"
}

_start_tunnel() {                  # sets TURL to the https://...trycloudflare.com url
  local cf logf i
  cf="$(_ensure_cloudflared)" || { echo "  Couldn't get cloudflared (no network?)." >&2; return 1; }
  logf="/tmp/cs460-cf-tunnel.log"
  setsid "$cf" tunnel --no-autoupdate --url "http://127.0.0.1:${PORT}" >"$logf" 2>&1 &
  echo $! > /tmp/cs460-cf-tunnel.pid
  for i in $(seq 1 30); do
    TURL="$(grep -oE 'https://[a-z0-9-]+\.trycloudflare\.com' "$logf" 2>/dev/null | head -1)"
    [ -n "$TURL" ] && return 0
    sleep 1
  done
  echo "  Tunnel did not come up in time; see $logf" >&2
  return 1
}

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

# The bridge mints a per-session key; the page and the WebSocket both require it, so the
# link/QR below is what carries access. A bare guess of the public URL gets a "needs key"
# notice, not the sharing UI or anyone's sensors.
K="$(cat /tmp/bridge.key 2>/dev/null || true)"
KQ=""; [ -n "$K" ] && KQ="?k=${K}"

# 2. public URL. Exposure is opt-in: it happens HERE, when the student runs this command
#    (and, locally, only with --tunnel), not automatically on container start.
if [ -n "${CODESPACE_NAME:-}" ]; then
  URL="https://${CODESPACE_NAME}-${PORT}.app.github.dev/${KQ}"
  if command -v gh >/dev/null 2>&1 && gh codespace ports visibility "${PORT}:public" -c "$CODESPACE_NAME" >/dev/null 2>&1; then
    PUB="public"
  else
    PUB="NOT-public"
  fi
elif [ "$TUNNEL" = 1 ]; then
  # OPT-IN public tunnel for a local Dev Container (student asked for it with --tunnel).
  if _start_tunnel; then
    URL="${TURL}/${KQ}"
    PUB="tunnel"
  else
    URL="http://localhost:${PORT}/${KQ}"
    PUB="local"
    echo "  (falling back to the localhost URL — the tunnel couldn't start)"
  fi
else
  URL="http://localhost:${PORT}/${KQ}"
  PUB="local"
fi

# 3. QR in the terminal
echo
node -e "require('./node_modules/qrcode').toString('${URL}',{type:'terminal',small:true},(e,s)=>{if(e){console.error(e.message);process.exit(1)}process.stdout.write(s)})"
echo
echo "  Scan the QR above with your phone, or open:"
echo "    ${URL}"
echo
echo "  Then, at the plc> console (./qemu/console.sh), point the twin at this broker:"
echo "    mqtt connect 127.0.0.1        (saved; 'mqtt disconnect' stops it and forgets it)"
echo "    mqtt status                   (every phone point, its age and fresh flag)"
echo "    mqtt pin <phone id>           (optional: accept only the id shown on your phone)"
echo
if [ "${PUB:-}" = "public" ]; then
  echo "  HEADS UP: :${PORT} is now PUBLIC. Anyone with this link can open the page and see"
  echo "  the sensors you choose to Share. It stays public until you stop the bridge or run"
  echo "    gh codespace ports visibility ${PORT}:private -c \"\$CODESPACE_NAME\""
fi
if [ "${PUB:-}" = "NOT-public" ]; then
  echo
  echo "  NOTE: couldn't confirm :${PORT} is public. If the phone hits a GitHub login page,"
  echo "        open the Ports panel, set ${PORT} -> Public (or Rebuild Container to automate it)."
fi
if [ "${PUB:-}" = "tunnel" ]; then
  echo "  HEADS UP: a PUBLIC tunnel is now open via Cloudflare's free, no-account quick"
  echo "  tunnel (the \"trial\" edition). Anyone with the link above can open the page and"
  echo "  see the sensors you choose to Share, and your phone's readings travel through"
  echo "  Cloudflare. The link is random and temporary — it disappears when you stop the"
  echo "  tunnel or close this terminal."
  echo "    stop it:  kill \$(cat /tmp/cs460-cf-tunnel.pid)"
fi
if [ "${PUB:-}" = "local" ]; then
  echo "  NOTE: ${URL} opens from a browser ON THIS LAPTOP. A phone can't reach a laptop's"
  echo "  'localhost', so the QR won't work from your phone as-is. Two options:"
  echo "    • Phone, from any network:  bash mqtt/share.sh --tunnel"
  echo "        opens a free Cloudflare quick tunnel (a temporary PUBLIC url; your sensor"
  echo "        readings pass through Cloudflare). Opt-in — nothing is exposed until you run it."
  echo "    • No phone:  drive the twin's modelled inputs instead ( FIELD_ANALOG=1 ./qemu/run.sh )."
fi
echo
