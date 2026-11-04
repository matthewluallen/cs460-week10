#!/usr/bin/env bash
# Quick launch — one command from an open Codespace / Dev Container to a running lab:
#   ./start.sh            fetch the twin if needed, boot it, open the Logic Studio (+ console)
#   ./start.sh secure     same, but the hardened Part-9 build (vuln is the default)
#   ./start.sh --info     just print the lab map + environment check; launch nothing
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; cd "$HERE"

MODE=launch; PROFILE=vuln
case "${1:-}" in
  --info|--no-launch|-n) MODE=info ;;
  secure|malicious|vuln) PROFILE="$1" ;;
esac

cat <<'EOF'
CS460 Week 10 — StampLC: SBOM/HBOM + Bus & Firmware Analysis

  A · Build
  Part 1  wiring/         drive YOUR twin from the Logic Studio  ./qemu/studio.sh (twin)
  Part 2  ladder/         change one rung in ladder/passthrough.st       (no device)
  B · Analyze
  Part 3  bus-analysis/   decode a real Modbus RTU exchange              (no device)
  Part 4  hbom/           complete hbom/stamplc-hbom.csv                 (no device)
  Part 5  sbom/           bash sbom/run_sbom.sh                          (no device)
  C · Break
  Part 6  firmware-ota/   boot the twin; find the unsigned-OTA bug       (twin)
  Part 7  supply-chain/   detonate a dormant OTA kill switch             (twin)
  Part 8  ntp-aitm/       spoof network time; wrap the clock to 1902     (twin)
  D · Secure
  Part 9  remediation/    flip the secure build; watch the attacks fail  (twin)
  Part 10 formal-verify/  prove every control with Z3 (Level 3, Week 12)  (no device)

Helper scripts (Parts 6-9): short to type, annotated inside, and they print the
full command before running it — so you see it without typing it. See qemu/README.md.

Environment check:
EOF
node bus-analysis/tools/decode.js rtu-request bus-analysis/serial/physical-0-request.bin >/dev/null 2>&1 \
  && echo "  bus decoder : OK" || echo "  bus decoder : FAILED (is Node installed?)"
command -v syft  >/dev/null && echo "  syft        : OK" || echo "  syft        : not found (runs in the devcontainer)"
command -v grype >/dev/null && echo "  grype       : OK" || echo "  grype       : not found (runs in the devcontainer)"
command -v pio   >/dev/null && echo "  platformio  : OK" || echo "  platformio  : not found (runs in the devcontainer; Part 9)"
if ls qemu/twin/qemu-twin-*/qemu-system-xtensa >/dev/null 2>&1 && [ -f qemu/twin/stamplc-flash-8MB.bin ]; then
  echo "  twin        : ready"
else
  echo "  twin        : not fetched (./start.sh will fetch it)"
fi
if [ -x .devcontainer/prefetch.sh ]; then
  echo
  bash .devcontainer/prefetch.sh --status 2>/dev/null
  echo "  (heavy later-part tools load in the background, in the open — see .devcontainer/ENVIRONMENT.md)"
fi
echo

if [ "$MODE" = info ]; then
  echo "Open README.md and work the parts in order.  Run ./start.sh (no args) to launch the twin + Studio."
  exit 0
fi

# ── quick launch ─────────────────────────────────────────────────────────────
# Always fetch: this guarantees the live released twin, not a stale copy that a
# prebuilt devcontainer image may have baked in. fetch-qemu is quick and idempotent.
echo "Fetching the current twin + firmware (so you run the live release, not a stale baked copy)…"
if ! ./qemu/fetch-qemu.sh; then
  if ls qemu/twin/qemu-twin-*/qemu-system-xtensa >/dev/null 2>&1 && [ -f qemu/twin/stamplc-flash-8MB.bin ]; then
    echo "  fetch failed (offline?) — continuing with the twin already present."
  else
    echo "  fetch failed and no twin is present — fix connectivity, then re-run ./start.sh"; exit 1
  fi
fi
echo
echo "Launching the twin + Logic Studio ($PROFILE build)…"
echo "  Open the Studio:  Codespaces → the auto-forwarded port 8088 (popup / Ports tab)"
echo "                    local VS Code → http://localhost:8088"
echo "  Text console (optional, another terminal):  ./qemu/console.sh"
echo "  Phone-sensor part (later):  bash mqtt/share.sh        (local Dev Container: add --tunnel)"
echo "  Keepalive  : while you use the Studio tab this Codespace stays awake; leave or close it and it idles out on its own, so idle credits are not spent."
echo "  Ctrl-C stops the Studio server; the twin keeps running (./qemu/twin-status.sh)."
echo
exec ./qemu/studio.sh "$PROFILE"
