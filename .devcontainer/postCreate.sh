#!/usr/bin/env bash
# CS460 Week 10 lab — runtime wiring for the PREBUILT devcontainer image.
# Runs identically in GitHub Codespaces and local VS Code Dev Containers (x86_64 or arm64).
#
# The heavy provisioning (apt tooling, Node/gh, syft/grype/trivy, z3/pymodbus/pyserial,
# Playwright Chromium, PlatformIO + the ESP32-S3 toolchain, and the QEMU twin) is BAKED into
# ghcr.io/matthewluallen/cs460-week10-devcontainer by .devcontainer/Dockerfile. So this script
# only does the few things that depend on the mounted repo: link the baked twin into qemu/twin,
# put `pio` on PATH, and make the `playwright` npm package require-able for verify-lab.py P2.
# Each step falls back to a full install if its baked asset is somehow absent, so the script is
# safe even when run outside the prebuilt image.
set -euo pipefail

BIN=/usr/local/bin
BAKED_TWIN=/opt/cs460/twin

echo "==> Linking the baked QEMU twin into qemu/twin (no download)…"
# The image stages the twin at /opt/cs460/twin (this image's native arch). qemu/twin/ is gitignored
# and overmounted by the repo at runtime, so link the baked binary dir + flash images into it. run.sh
# then finds qemu/twin/qemu-twin-linux-<arch>/qemu-system-xtensa and qemu/twin/stamplc-flash-8MB.bin.
if [ -d "$BAKED_TWIN" ]; then
  mkdir -p qemu/twin
  # arch binary dir (qemu-twin-linux-x86_64 or -arm64)
  for d in "$BAKED_TWIN"/qemu-twin-linux-*; do
    [ -d "$d" ] && ln -sfn "$d" "qemu/twin/$(basename "$d")"
  done
  # flash images (vulnerable + optional malicious)
  for f in stamplc-flash-8MB.bin stamplc-flash-8MB-MALICIOUS.bin; do
    [ -e "$BAKED_TWIN/$f" ] && ln -sfn "$BAKED_TWIN/$f" "qemu/twin/$f"
  done
  echo "   twin linked from $BAKED_TWIN ($(uname -m))"
else
  echo "   no baked twin found — fetching at runtime (fallback)…"
  bash qemu/fetch-qemu.sh || echo "   (twin fetch skipped — run ./qemu/fetch-qemu.sh later; see qemu/README.md)"
fi

echo "==> Putting PlatformIO on PATH…"
# PlatformIO + the ESP32-S3 toolchain are baked in ~/.platformio (warm-built). Expose the docs'
# bare `pio run -e m5stamplc_secure` by symlinking pio onto PATH. Fall back to installing it.
PIO="$HOME/.platformio/penv/bin/pio"
if [ ! -x "$PIO" ]; then
  echo "   no baked PlatformIO — installing at runtime (fallback)…"
  curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py \
    && python3 /tmp/get-platformio.py \
    || echo "   (PlatformIO install skipped — see sbom/firmware/BUILD.md)"
fi
[ -x "$PIO" ] && sudo ln -sf "$PIO" "$BIN/pio" 2>/dev/null || true

echo "==> Ensuring assessment libraries are importable (z3, pymodbus, pyserial)…"
# The prebuilt image installs these into the PlatformIO penv (the python3 that ends up on PATH).
# If an older cached image is missing them, self-heal by installing into the active python. This is
# a no-op (fast import check) on a current image, so it does not slow normal startup.
python3 -c "import z3, pymodbus, serial" 2>/dev/null \
  && echo "   z3 + pymodbus + pyserial OK (baked)" \
  || { echo "   installing z3-solver/pymodbus/pyserial into the active python…"; \
       pip3 install --no-cache-dir z3-solver pymodbus pyserial >/dev/null 2>&1 \
         && echo "   installed" || echo "   (install skipped — Part 10 needs: pip install z3-solver)"; }

echo "==> Making Playwright require-able for verify-lab.py P2…"
# The Chromium browser is baked at /ms-playwright (PLAYWRIGHT_BROWSERS_PATH is set in the image), so
# this only installs the `playwright` npm package into ./node_modules (gitignored, --no-save). Pin it
# to the baked version so it reuses the baked browser instead of downloading one. Non-fatal.
if command -v npm >/dev/null 2>&1; then
  npm i --no-save --no-package-lock "playwright@${PLAYWRIGHT_VERSION:-1.48.2}" >/dev/null 2>&1 \
    && echo "   playwright npm package linked (browser already baked at ${PLAYWRIGHT_BROWSERS_PATH:-/ms-playwright})" \
    || echo "   playwright link skipped — verify-lab.py P2 will SKIP until:  npm i playwright && npx playwright install chromium"
else
  echo "   npm not found — skipping playwright (verify-lab.py P2 will SKIP until installed)"
fi

echo "==> Verifying the bus-analysis decoder (pure Node, no device)…"
node bus-analysis/tools/decode.js rtu-request bus-analysis/serial/physical-0-request.bin >/dev/null \
  && echo "   bus-analysis decoder OK" \
  || echo "   bus-analysis decoder FAILED — check the image's Node install"

# ── status summary ───────────────────────────────────────────────────────────
TWIN="not linked (run ./qemu/fetch-qemu.sh)"
[ -e qemu/twin/stamplc-flash-8MB.bin ] && TWIN="ready for $(uname -m)  →  ./qemu/run.sh"
PIOST="not installed"
[ -x "$PIO" ] && PIOST="installed + toolchain cached (baked)"

ver() {   # first line of "<tool> <args>" or "not installed" (no pipes: safe under pipefail)
  command -v "$1" >/dev/null 2>&1 || { echo "not installed"; return 0; }
  local out; out="$("$@" 2>/dev/null || true)"
  out="${out#"${out%%[![:space:]]*}"}"
  out="${out%%$'\n'*}"
  echo "${out:-installed (version unknown)}"
}
echo
echo "==> Tool versions (baked into the image)"
printf '   %-13s %s\n' "tshark:"       "$(ver tshark --version)"
printf '   %-13s %s\n' "sigrok-cli:"   "$(ver sigrok-cli --version)"
printf '   %-13s %s\n' "syft:"         "$(ver syft --version)"
printf '   %-13s %s\n' "grype:"        "$(ver grype --version)"
printf '   %-13s %s\n' "trivy:"        "$(ver trivy --version)"
printf '   %-13s %s\n' "cve-bin-tool:" "$(PATH="$HOME/.local/bin:$PATH" ver cve-bin-tool --version)"
printf '   %-13s %s\n' "pio:"          "$(ver pio --version)"
printf '   %-13s %s\n' "python3:"      "$(ver python3 --version)"
printf '   %-13s %s\n' "node:"         "$(ver node --version)"

echo
echo "============================================================"
echo " CS460 Weeks 10–12 lab ready (prebuilt image)."
echo "   syft:  $(command -v syft  >/dev/null && echo installed || echo 'not installed')    grype: $(command -v grype >/dev/null && echo installed || echo 'not installed')"
echo "   z3:    $(python3 -c 'import z3' 2>/dev/null && echo 'installed (Part 10 formal verification)' || echo 'not installed — pip install z3-solver')"
echo "   sigrok:$(command -v sigrok-cli >/dev/null && echo ' installed' || echo ' not installed')  tshark:$(command -v tshark >/dev/null && echo ' installed (capture: sudo tshark)' || echo ' not installed')  node: $(node --version)   python: $(python3 --version)"
echo "   pio:   $PIOST"
echo "   twin:  $TWIN"
echo
echo " Start here:  open README.md   (or run:  ./start.sh)"
echo "============================================================"
