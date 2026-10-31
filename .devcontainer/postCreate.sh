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
# trivy is an OPTIONAL second scanner (Exercise 2 works with syft+grype alone). Try the
# official install.sh, then fall back to the release tarball, then give up quietly.
curl -sSfL https://raw.githubusercontent.com/aquasecurity/trivy/main/contrib/install.sh | sudo sh -s -- -b "$BIN" "$TRIVY_VERSION" 2>/dev/null \
  || { case "$(uname -m)" in aarch64|arm64) TA=Linux-ARM64;; *) TA=Linux-64bit;; esac; \
       curl -sSfL "https://github.com/aquasecurity/trivy/releases/download/${TRIVY_VERSION}/trivy_${TRIVY_VERSION#v}_${TA}.tar.gz" -o /tmp/trivy.tgz 2>/dev/null \
       && sudo tar -xzf /tmp/trivy.tgz -C "$BIN" trivy 2>/dev/null && echo "   trivy installed (tarball fallback)"; } \
  || echo "   (trivy optional — skipped; syft+grype cover Exercise 2)"

echo "==> Installing cve-bin-tool (binary CVE scanner — Part 5 Exercise 3) + gsutil (for the CVE mirror)…"
# cve-bin-tool fingerprints a compiled .bin for component versions. pipx keeps it isolated; fall
# back to pip. gsutil lets it pull CVE data from the json-mirror (a public GCS bucket) instead of
# the NVD API — so Exercise 3 needs NO NVD key and works even when nvd.nist.gov is down. Non-fatal.
pipx install cve-bin-tool >/dev/null 2>&1 \
  || pip3 install --quiet --break-system-packages cve-bin-tool 2>/dev/null \
  || pip3 install --quiet cve-bin-tool 2>/dev/null \
  || echo "   (cve-bin-tool install skipped — see sbom/README.md Exercise 3)"
pip3 install --quiet gsutil 2>/dev/null || pip3 install --quiet --break-system-packages gsutil 2>/dev/null \
  || echo "   (gsutil install skipped — Exercise 3 notes the fallback)"

echo "==> Installing SAST tools for Part 5 Exercise 4 (flawfinder; cppcheck came via apt above)…"
# flawfinder = CWE-tagged risky-C/C++ pattern scanner. cppcheck (apt, above) = bug/style SAST.
# Exercise 4 runs both to show SAST finds code-pattern weaknesses but MISSES the firmware's
# architectural CWEs (no-auth / cleartext / unsigned-OTA / default creds). Non-fatal.
pip3 install --quiet flawfinder 2>/dev/null || pip3 install --quiet --break-system-packages flawfinder 2>/dev/null \
  || echo "   (flawfinder install skipped — see sbom/README.md Exercise 4)"

# Pre-cache the mirror CVE data so the first Exercise-3 scan is quick. json-mirror = cve-bin-tool's
# public CVE snapshot (no NVD key, no NVD API). GAD (GitLab advisories) is disabled: its multi-doc
# YAML crashes cve-bin-tool's parser. Non-fatal; the offline 'strings' proof always works.
if command -v cve-bin-tool >/dev/null 2>&1; then
  echo "==> Pre-caching the CVE mirror + warming the Exercise-3 scan (no NVD key needed)…"
  FW=sbom/firmware/prebuilt/stamplc-factory-8MB.bin
  mkdir -p sbom/out
  { [ -f "$FW" ] && PATH="$HOME/.local/bin:$PATH" cve-bin-tool "$FW" -n json-mirror --disable-data-source GAD \
      -f html -o sbom/out/cve-bin-tool-firmware.html >/dev/null 2>&1 \
      && echo "   CVE mirror cached; report pre-generated at sbom/out/cve-bin-tool-firmware.html"; } \
    || echo "   (CVE mirror pre-cache skipped — run 'bash sbom/run_cve_bin_tool.sh' later)"
fi

echo "==> Installing bus & network tooling (sigrok-cli, tshark, nc, cppcheck)…"
sudo apt-get update -qq || true
# tshark's package asks "Should non-superusers be able to capture packets?" (wireshark-common/
# install-setuid). Answer it BEFORE installing so the Codespace build never stops at a dialog.
# false = capturing stays root-only: students run  sudo tshark -i lo -f "tcp port 502"  (reading a
# saved .pcap needs no sudo). sigrok-cli decodes the live I2C tap (./bus-analysis/tap.sh).
echo "wireshark-common wireshark-common/install-setuid boolean false" | sudo debconf-set-selections
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
    sigrok-cli tshark xz-utils netcat-openbsd cppcheck >/dev/null 2>&1 \
  || sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq sigrok-cli tshark xz-utils netcat-openbsd cppcheck >/dev/null 2>&1 \
  || echo "   (some apt packages skipped — run: sudo apt-get install -y sigrok-cli tshark)"
pip3 install --quiet --break-system-packages pymodbus 2>/dev/null \
  || pip3 install --quiet pymodbus 2>/dev/null \
  || echo "   (pip pymodbus skipped — Part 6 notes the HMI fallback)"

echo "==> Installing formal-verification + assessment tooling (Z3 solver, pyserial)…"
# Week 12 / Level 3 proves the security controls with Z3 (formal-verify/verify_controls.py).
# pyserial lets assessment/verify-lab.py drive a PHYSICAL StampLC over USB (the twin needs none).
# Both non-fatal — 'pip install z3-solver pyserial' later if the network blocks it here.
pip3 install --quiet --break-system-packages z3-solver pyserial 2>/dev/null \
  || pip3 install --quiet z3-solver pyserial 2>/dev/null \
  || echo "   (z3-solver/pyserial install skipped — see week12/README.md)"

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

ver() {   # first line of "<tool> <args>" or "not installed" (no pipes: safe under pipefail)
  command -v "$1" >/dev/null 2>&1 || { echo "not installed"; return 0; }
  local out; out="$("$@" 2>/dev/null || true)"
  out="${out#"${out%%[![:space:]]*}"}"
  out="${out%%$'\n'*}"
  echo "${out:-installed (version unknown)}"
}
echo
echo "==> Tool versions"
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
echo " CS460 Weeks 10–12 lab ready."
echo "   syft:  $(command -v syft  >/dev/null && echo installed || echo 'not installed')    grype: $(command -v grype >/dev/null && echo installed || echo 'not installed')"
echo "   z3:    $(python3 -c 'import z3' 2>/dev/null && echo 'installed (Week 12 formal verification)' || echo 'not installed — pip install z3-solver')"
echo "   sigrok:$(command -v sigrok-cli >/dev/null && echo ' installed' || echo ' not installed')  tshark:$(command -v tshark >/dev/null && echo ' installed (capture: sudo tshark)' || echo ' not installed')  node: $(node --version)   python: $(python3 --version)"
echo "   cve-bin-tool:$(command -v cve-bin-tool >/dev/null && echo ' installed' || echo ' not installed')  flawfinder:$(command -v flawfinder >/dev/null && echo ' installed' || echo ' not installed')  cppcheck:$(command -v cppcheck >/dev/null && echo ' installed' || echo ' not installed')  trivy:$(command -v trivy >/dev/null && echo ' installed' || echo ' optional/absent')"
echo "   pio:   $PIOST"
echo "   twin:  $TWIN"
echo
echo " Start here:  open README.md   (or run:  ./start.sh)"
echo "============================================================"
