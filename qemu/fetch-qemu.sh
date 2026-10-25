#!/usr/bin/env bash
# Fetch the prebuilt StampLC QEMU twin for THIS platform:
# the patched qemu-system-xtensa + its ROM + the 8MB firmware image.
# Optional — only needed to run the firmware in QEMU (see ../docs/QEMU-Twin.md).
set -euo pipefail
REPO="${QEMU_TWIN_REPO:-matthewluallen/cs460-week10}"
TAG="${QEMU_TWIN_TAG:-qemu-twin-v1}"
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="$HERE/twin"
mkdir -p "$DEST"

OS=$(uname -s); ARCH=$(uname -m)
case "$OS/$ARCH" in
  Linux/x86_64)  ASSET=qemu-twin-linux-x86_64.tar.xz ;;
  Linux/aarch64) ASSET=qemu-twin-linux-arm64.tar.xz ;;   # local Dev Container on Apple Silicon
  Darwin/arm64)  ASSET=qemu-twin-macos-arm64.tar.xz ;;
  *) echo "No prebuilt twin for $OS/$ARCH — build from source (see ../docs/QEMU-Twin.md)."; exit 1 ;;
esac

echo "==> Fetching $ASSET + firmware image from release $TAG…"
# Download one asset: try gh first (auth'd — needed while the repo is private, e.g.
# a fresh Codespace), then fall back to a plain curl of the public release URL. A
# local Dev Container usually has gh INSTALLED but UNAUTHENTICATED, so gh fails there
# and the curl fallback (which works once the repo is public) must still run.
BASE="https://github.com/$REPO/releases/download/$TAG"
dl() {   # $1 = asset filename
  local a="$1"
  if command -v gh >/dev/null 2>&1 && gh release download "$TAG" -R "$REPO" -p "$a" -D "$DEST" --clobber 2>/dev/null; then
    return 0
  fi
  curl -sSfL "$BASE/$a" -o "$DEST/$a"
}
dl "$ASSET"
dl "stamplc-flash-8MB.bin.xz"
dl "stamplc-flash-8MB-MALICIOUS.bin.xz" || echo "   (malicious image not published — Part 7 notes the fallback)"

tar xJf "$DEST/$ASSET" -C "$DEST"
xz -dkf "$DEST/stamplc-flash-8MB.bin.xz"
[ -f "$DEST/stamplc-flash-8MB-MALICIOUS.bin.xz" ] && xz -dkf "$DEST/stamplc-flash-8MB-MALICIOUS.bin.xz" || true

if [ "$OS" = Linux ] && command -v apt-get >/dev/null 2>&1; then
  echo "==> Installing QEMU runtime libraries + libcap2-bin…"
  sudo apt-get update -qq && sudo apt-get install -y -qq libglib2.0-0 libpixman-1-0 libslirp0 libgcrypt20 libcap2-bin \
    || echo "   (install libglib2.0-0 libpixman-1-0 libslirp0 libgcrypt20 libcap2-bin manually if run fails)"

  # Least privilege: let the unprivileged emulator bind the OTA port (80).
  # The W5500 bridge binds the guest's listen ports on the host; 80 is privileged,
  # and Codespaces won't let us lower net.ipv4.ip_unprivileged_port_start (read-only).
  # Grant JUST cap_net_bind_service to the qemu binary — nothing else, and no root
  # at run time. (run.sh re-applies this if it's ever missing.)
  QEMU_BIN="$(ls "$DEST"/qemu-twin-linux-*/qemu-system-xtensa 2>/dev/null | head -1 || true)"
  if [ -n "$QEMU_BIN" ] && command -v setcap >/dev/null 2>&1; then
    sudo setcap cap_net_bind_service=+ep "$QEMU_BIN" 2>/dev/null \
      && echo "   granted cap_net_bind_service to the twin (OTA :80 bindable without root)" \
      || echo "   NOTE: couldn't set cap_net_bind_service — OTA :80 may need a privileged bind (see qemu/README.md)"
  fi
fi

echo "==> Done. Run:  ./qemu/run.sh     (then in another shell:  ./qemu/console.sh)"
