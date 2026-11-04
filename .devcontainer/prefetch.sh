#!/usr/bin/env bash
# Transparent "backload" of the heavy, LATER-needed lab tools.
#
# WHY: the labs are sequential. Part 1 (wire & run) needs only the twin + Logic Studio,
# which ./start.sh brings up in seconds. The big downloads belong to later parts:
#   - the grype CVE database  -> Part 5 (SBOM vulnerability scan)
#   - the PlatformIO ESP32-S3 toolchain -> Part 9 (secure build)
#   - Playwright Chromium -> the optional Part-2 self-check
# So we DON'T make you wait for them up front. They download in the background while you
# work the early parts — and, because this is a security course, they download IN THE OPEN:
# this script says exactly WHAT it fetches, WHY, and FROM WHERE, and logs every step.
#
#   watch it:    tail -f ~/.cache/cs460-prefetch/prefetch.log
#   status:      bash .devcontainer/prefetch.sh --status   (also shown by ./start.sh --info)
#
# Nothing here is required for the lab to work: if a download hasn't finished when you reach
# its part, that part's own script fetches it on demand (with its own progress). This script
# only moves the wait earlier and off your critical path. It is safe to re-run and idempotent.
set -uo pipefail
C="$HOME/.cache/cs460-prefetch"; mkdir -p "$C"; LOG="$C/prefetch.log"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT" 2>/dev/null || true

if [ "${1:-}" = "--status" ]; then
  echo "lab environment backload — status ($(date +%H:%M:%S)):"
  for t in sigrok grype-cve-db platformio-esp32s3 playwright-chromium; do
    if   [ -f "$C/$t.done" ];    then echo "  [ready]       $t"
    elif [ -f "$C/$t.running" ]; then echo "  [downloading] $t   (watch: tail -f $LOG)"
    else                              echo "  [queued]      $t"; fi
  done
  exit 0
fi

exec >>"$LOG" 2>&1
echo
echo "================ backload started $(date) (pid $$) ================"
echo "Part 1 is already usable (./start.sh brings up the twin + Studio). The tools below"
echo "download in the open while you work; each line shows what, why, and from where."

step() {   # step <id> <needed-by> <source> <cmd...>
  local id="$1" need="$2" src="$3"; shift 3
  if [ -f "$C/$id.done" ]; then echo "[$(date +%H:%M:%S)] $id: already present — skip."; return 0; fi
  : > "$C/$id.running"
  echo "[$(date +%H:%M:%S)] $id  — needed by $need — source: $src"
  if "$@"; then echo "[$(date +%H:%M:%S)] $id: READY"; touch "$C/$id.done"
  else           echo "[$(date +%H:%M:%S)] $id: not completed — its part will fetch it on demand"; fi
  rm -f "$C/$id.running"
}

# Order = the order the parts reach them.
step sigrok              "Part 3 (bus decode)"              "Ubuntu apt (sigrok-cli)" \
    bash -c 'command -v sigrok-cli >/dev/null 2>&1 || { sudo apt-get update -qq && sudo apt-get install -y -qq sigrok-cli; }'

step grype-cve-db        "Part 5 (SBOM CVE scan)"           "anchore grype vulnerability DB" \
    bash -c 'command -v grype >/dev/null 2>&1 && grype db update'

step platformio-esp32s3  "Part 9 (secure build)"            "PlatformIO registry (ESP32-S3 platform + Xtensa toolchain)" \
    bash -c 'command -v pio >/dev/null 2>&1 && pio pkg install -d sbom/firmware -e m5stamplc_secure'

step playwright-chromium "Part 2 self-check (optional)"     "Playwright CDN (headless Chromium)" \
    bash -c '[ -d node_modules ] && npx --yes playwright install chromium >/dev/null 2>&1 || true'

echo "================ backload finished $(date) ================"
