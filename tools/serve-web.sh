#!/usr/bin/env bash
# serve-web.sh — serve the web/public folder so you can open the HTML tools
# (logic_studio.html, i2c-analyzer.html, panel.html, ...) on a forwarded port.
# Shared infra: referenced by several Week-10 parts. Tunable: PORT (default 8000).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"            # tools/
source "$HERE/teach.sh" 2>/dev/null || true
declare -F step    >/dev/null 2>&1 || step()    { printf '\n==== %s ====\n' "$*"; }
declare -F say     >/dev/null 2>&1 || say()     { printf '>  %s\n' "$*"; }
declare -F run     >/dev/null 2>&1 || run()     { printf '   $ %s\n' "$*"; "$@"; }
declare -F observe >/dev/null 2>&1 || observe() { printf '-> %s\n' "$*"; }
declare -F predict >/dev/null 2>&1 || predict() { printf '?  PREDICT FIRST: %s\n' "$*"; }
declare -F finding >/dev/null 2>&1 || finding() { printf 'FINDING: %s\n' "$*"; }
declare -F note    >/dev/null 2>&1 || note()    { printf '   %s\n' "$*"; }
ROOT="$(cd "$HERE/.." && pwd)"                    # repo root
PORT="${PORT:-8000}"

step "Serve web/public for the browser-based lab tools"
say "Static file server only — it serves local HTML; it does not connect to the twin or any device. Forward port $PORT in Codespaces, then open the page you want."
observe "The tools run entirely in your browser against files/captures you load; serving them is not connecting to hardware."
note "Press Ctrl-C to stop the server when you're done."
run python3 -m http.server -d "$ROOT/web/public" "$PORT"
