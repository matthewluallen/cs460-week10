#!/usr/bin/env bash
# CS460 Week 10 lab — one-time environment setup.
# Runs identically in GitHub Codespaces and local VS Code Dev Containers (x86_64 or arm64).
# Provisions the FULL lab: SBOM tools, the bus decoder, and the firmware twin (QEMU).
set -euo pipefail

BIN=/usr/local/bin
SYFT_VERSION=v1.55.0
GRYPE_VERSION=v0.121.0
TRIVY_VERSION=v0.58.0

echo "==> Installing SBOM / vulnerability tools (syft, grype, trivy)…"
# Official pinned installers. If your network blocks raw.githubusercontent.com,
# see sbom/README.md for the direct-download fallback.
curl -sSfL https://raw.githubusercontent.com/anchore/syft/main/install.sh  | sudo sh -s -- -b "$BIN" "$SYFT_VERSION"  || echo "   (syft install skipped — see sbom/README.md)"
curl -sSfL https://raw.githubusercontent.com/anchore/grype/main/install.sh | sudo sh -s -- -b "$BIN" "$GRYPE_VERSION" || echo "   (grype install skipped — see sbom/README.md)"
curl -sSfL https://raw.githubusercontent.com/aquasecurity/trivy/main/contrib/install.sh | sudo sh -s -- -b "$BIN" "$TRIVY_VERSION" || echo "   (trivy install skipped — see sbom/README.md)"

echo "==> Installing cve-bin-tool (binary CVE scanner — Part 2 Exercise 3)…"
# Scans the COMPILED firmware .bin for embedded ESP-IDF components (mbedTLS/lwIP/…).
# pipx keeps it isolated; fall back to pip. Non-fatal — Exercise 3 notes the offline path.
pipx install cve-bin-tool >/dev/null 2>&1 \
  || pip3 install --quiet --break-system-packages cve-bin-tool 2>/dev/null \
  || pip3 install --quiet cve-bin-tool 2>/dev/null \
  || echo "   (cve-bin-tool install skipped — see sbom/README.md Exercise 3)"

echo "==> Installing bus & firmware tooling (sigrok decoder, pymodbus, nc)…"
sudo apt-get update -qq || true
sudo apt-get install -y -qq sigrok-cli xz-utils netcat-openbsd >/dev/null 2>&1 \
  || echo "   (some apt packages skipped — Parts 1/6 note fallbacks)"
pip3 install --quiet --break-system-packages pymodbus 2>/dev/null \
  || pip3 install --quiet pymodbus 2>/dev/null \
  || echo "   (pip pymodbus skipped — Part 6 notes the HMI fallback)"

echo "==> Fetching the firmware twin (QEMU) for $(uname -s)/$(uname -m)…"
# Downloads the patched qemu-system-xtensa + the 8 MB image(s) for this platform
# and installs the QEMU runtime libraries. Non-fatal: Parts 1–5 need no device.
bash qemu/fetch-qemu.sh \
  || echo "   (twin fetch skipped — run ./qemu/fetch-qemu.sh later; see qemu/README.md)"

echo "==> Installing PlatformIO + pre-caching the ESP32-S3 toolchain (fast Part-9 builds)…"
# Part 9 rebuilds the firmware (pio run -e m5stamplc_secure). Pre-download the
# pioarduino platform, the Xtensa toolchain and all libraries now — and warm the
# build cache with one teaching-env build — so the student's build compiles
# immediately with NO downloads. Non-fatal: skips cleanly if the network blocks it.
if [ ! -x "$HOME/.platformio/penv/bin/pio" ]; then
  curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py \
    && python3 /tmp/get-platformio.py \
    || echo "   (PlatformIO install skipped — see sbom/firmware/BUILD.md)"
fi
PIO="$HOME/.platformio/penv/bin/pio"
# Put `pio` on PATH so the Part-9 docs' bare `pio run -e m5stamplc_secure` just works.
[ -x "$PIO" ] && sudo ln -sf "$PIO" "$BIN/pio" 2>/dev/null || true
if [ -x "$PIO" ]; then
  # One warm build of the teaching env downloads the platform + toolchain + libs that
  # BOTH envs share (the secure env only toggles -D flags), and caches the objects.
  "$PIO" run -d sbom/firmware -e m5stamplc \
    && echo "   PlatformIO + ESP32-S3 toolchain cached (Part-9 build will be fast)" \
    || echo "   (pio warm-build skipped — run 'pio run -d sbom/firmware' later; see sbom/firmware/BUILD.md)"
fi

echo "==> Verifying the bus-analysis decoder (pure Node, no device)…"
node bus-analysis/tools/decode.js rtu-request bus-analysis/serial/physical-0-request.bin >/dev/null \
  && echo "   bus-analysis decoder OK" \
  || echo "   bus-analysis decoder FAILED — check Node install"

TWIN="not fetched (optional; Parts 1–5 need no device)"
[ -f qemu/twin/stamplc-flash-8MB.bin ] && TWIN="ready for $(uname -m)  →  ./qemu/run.sh"
PIOST="not installed"
[ -x "$HOME/.platformio/penv/bin/pio" ] && PIOST="installed + toolchain cached"

echo
echo "============================================================"
echo " CS460 Week 10 lab ready."
echo "   syft:  $(command -v syft  >/dev/null && echo installed || echo 'not installed')    grype: $(command -v grype >/dev/null && echo installed || echo 'not installed')"
echo "   sigrok:$(command -v sigrok-cli >/dev/null && echo ' installed' || echo ' not installed')  node: $(node --version)   python: $(python3 --version)"
echo "   pio:   $PIOST"
echo "   twin:  $TWIN"
echo
echo " Start here:  open README.md   (or run:  ./start.sh)"
echo "============================================================"
