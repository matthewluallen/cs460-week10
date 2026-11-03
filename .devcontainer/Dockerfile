# syntax=docker/dockerfile:1
#
# Prebuilt CS460 "Inside the Stack" devcontainer image.
#
# Bakes every heavy postCreate.sh step into image layers so a student's Codespace
# OR local VS Code Dev Container is ready after a *pull* instead of a ~10-minute build:
#   - bus/network + SAST tooling (sigrok-cli, tshark, cppcheck, flawfinder)
#   - SBOM / CVE scanners (syft, grype, trivy, cve-bin-tool, gsutil)
#   - Node 20, Python 3.12, GitHub CLI
#   - Z3 / pymodbus / pyserial
#   - PlatformIO + the ESP32-S3 toolchain, warmed by one teaching build (~/.platformio)
#   - Playwright headless Chromium (baked at /ms-playwright, shared read path)
#   - the v2 QEMU twin (+ flash images), staged at /opt/cs460/twin (out of the mount)
#
# Built and pushed to GHCR by .github/workflows/devcontainer-image.yml. The twin assets
# are placed in .devcontainer/twin-assets/ by that workflow (gh release download) before
# the build, so no registry auth is needed inside the build.
#
# NOTE: this image is wired into devcontainer.json in a SEPARATE follow-up PR (the
# "flip"), after the image is confirmed building and a test Codespace runs on it.

FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# 1) System packages: bus/network tooling, SAST, and the QEMU twin's runtime libraries.
#    The tshark setuid prompt is pre-answered (non-superusers cannot capture) so the
#    build never blocks on a debconf dialog.
RUN echo "wireshark-common wireshark-common/install-setuid boolean false" | debconf-set-selections \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl gnupg wget xz-utils \
      sigrok-cli tshark netcat-openbsd cppcheck \
      libglib2.0-0 libpixman-1-0 libslirp0 libgcrypt20 libcap2-bin libpng16-16 \
      python3-pip python3-venv \
 && rm -rf /var/lib/apt/lists/*

# 2) Node 20 (NodeSource) + GitHub CLI (official apt repo).
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
 && install -d -m 0755 /etc/apt/keyrings \
 && curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg -o /etc/apt/keyrings/githubcli-archive-keyring.gpg \
 && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
 && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
      > /etc/apt/sources.list.d/github-cli.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends nodejs gh \
 && rm -rf /var/lib/apt/lists/*

# 3) SBOM / vulnerability scanners -> /usr/local/bin (pinned, matching postCreate.sh).
ENV SYFT_VERSION=v1.55.0 GRYPE_VERSION=v0.121.0 TRIVY_VERSION=v0.58.0
# syft + grype are required; trivy is an OPTIONAL second scanner (syft+grype cover
# Exercise 2), so its installer's exit is tolerated — matching postCreate.sh.
RUN curl -sSfL https://raw.githubusercontent.com/anchore/syft/main/install.sh  | sh -s -- -b /usr/local/bin "$SYFT_VERSION" \
 && curl -sSfL https://raw.githubusercontent.com/anchore/grype/main/install.sh | sh -s -- -b /usr/local/bin "$GRYPE_VERSION" \
 && { curl -sSfL https://raw.githubusercontent.com/aquasecurity/trivy/main/contrib/install.sh | sh -s -- -b /usr/local/bin "$TRIVY_VERSION" \
      || echo "trivy optional — skipped (syft+grype cover Exercise 2)"; }

# 4) Python assessment + SBOM libraries.
#    z3-solver/pymodbus/pyserial are required; flawfinder/gsutil/cve-bin-tool are
#    optional SAST/CVE extras (Part 5 Exercises 3-4), tolerated like postCreate.sh.
RUN pip3 install --break-system-packages --no-cache-dir z3-solver pymodbus pyserial
RUN pip3 install --break-system-packages --no-cache-dir flawfinder gsutil cve-bin-tool \
 || echo "optional scanners (flawfinder/gsutil/cve-bin-tool) partially skipped"

# 5) Playwright headless Chromium for the Part-2 ladder regression (verify-lab.py P2).
#    Browser baked at a shared, world-readable path; OS deps installed here (needs root).
#    Pinned so the runtime `npm i playwright@${PLAYWRIGHT_VERSION}` matches and does NOT
#    re-download the browser.
ENV PLAYWRIGHT_BROWSERS_PATH=/ms-playwright
ENV PLAYWRIGHT_VERSION=1.48.2
RUN npx --yes "playwright@${PLAYWRIGHT_VERSION}" install --with-deps chromium \
 && chmod -R a+rX /ms-playwright

# 6) The v2 QEMU twin, staged OUT of the workspace mount at /opt/cs460/twin.
#    (qemu/twin/ is overmounted by the repo at runtime, so the baked copy lives here and
#    the thin runtime postCreate links it into qemu/twin instantly — no download.)
#    .devcontainer/twin-assets/ is populated by the CI workflow before the build, with the
#    tarball for THIS image's target arch (amd64 -> x86_64, arm64 -> arm64). The native
#    arm64 twin lets Apple-Silicon Macs run the twin natively instead of under emulation.
COPY .devcontainer/twin-assets/ /tmp/twin-assets/
ARG TARGETARCH
RUN set -eux; \
    case "${TARGETARCH}" in \
      amd64) TWIN_TARBALL=qemu-twin-linux-x86_64.tar.xz ;; \
      arm64) TWIN_TARBALL=qemu-twin-linux-arm64.tar.xz ;; \
      *) echo "unsupported TARGETARCH='${TARGETARCH}' (expected amd64 or arm64)" >&2; exit 1 ;; \
    esac; \
    mkdir -p /opt/cs460/twin; \
    tar xJf "/tmp/twin-assets/${TWIN_TARBALL}" -C /opt/cs460/twin; \
    xz -dkf /tmp/twin-assets/stamplc-flash-8MB.bin.xz; \
    cp /tmp/twin-assets/stamplc-flash-8MB.bin /opt/cs460/twin/; \
    ( xz -dkf /tmp/twin-assets/stamplc-flash-8MB-MALICIOUS.bin.xz \
        && cp /tmp/twin-assets/stamplc-flash-8MB-MALICIOUS.bin /opt/cs460/twin/ || true ); \
    setcap cap_net_bind_service=+ep "$(ls /opt/cs460/twin/qemu-twin-linux-*/qemu-system-xtensa | head -1)"; \
    chown -R vscode:vscode /opt/cs460; \
    rm -rf /tmp/twin-assets

# 7) PlatformIO core + ESP32-S3 toolchain, warmed by one teaching build (caches
#    ~/.platformio: platform, Xtensa toolchain, and libraries that both envs share).
USER vscode
RUN curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py \
 && python3 /tmp/get-platformio.py \
 && rm /tmp/get-platformio.py
ENV PATH=/home/vscode/.platformio/penv/bin:${PATH}
COPY --chown=vscode:vscode sbom/firmware /tmp/fw
RUN pio run -d /tmp/fw -e m5stamplc \
 && rm -rf /tmp/fw
USER root

LABEL org.opencontainers.image.title="CS460 Inside the Stack devcontainer" \
      org.opencontainers.image.description="Prebuilt lab environment (tools, PlatformIO+ESP32-S3 toolchain, Playwright Chromium, QEMU twin)." \
      org.opencontainers.image.source="https://github.com/matthewluallen/cs460-week10"
