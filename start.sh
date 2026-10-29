#!/usr/bin/env bash
cat <<'EOF'
CS460 Week 10 — StampLC: SBOM/HBOM + Bus & Firmware Analysis

  Part 1  bus-analysis/   decode a real Modbus RTU exchange              (no device)
  Part 2  sbom/           bash sbom/run_sbom.sh                          (no device)
  Part 3  hbom/           complete hbom/stamplc-hbom.csv                 (no device)
  Part 4  ladder/         change one rung in ladder/passthrough.st       (no device)
  Part 5  wiring/         drive YOUR twin from the Logic Studio  ./qemu/studio.sh (twin)
  Part 6  firmware-ota/   boot the twin; find the unsigned-OTA bug       (twin)
  Part 7  supply-chain/   detonate a dormant OTA kill switch             (twin)
  Part 8  ntp-aitm/       spoof network time; wrap the clock to 1902     (twin)
  Part 9  remediation/    flip the secure build; watch the attacks fail  (twin)

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
  echo "  twin        : ready  ->  ./qemu/studio.sh   (twin + Studio + console, one command)"
else
  echo "  twin        : not fetched ->  ./qemu/fetch-qemu.sh   (needed for Parts 6-9)"
fi
echo
echo "Open README.md and work the parts in order."
