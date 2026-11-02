#!/usr/bin/env bash
# ghidra.sh — one command: headless Ghidra decompilation of the teaching firmware.
#
# Usage:
#   ./firmware-re/ghidra.sh                                     # default: the firmware ELF (real symbols + decompiler)
#   ./firmware-re/ghidra.sh plc_force_coil                      # focus the decompile on matching function names
#   ./firmware-re/ghidra.sh qemu/twin/stamplc-secure-8MB.bin    # a specific flash image instead
#   ./firmware-re/ghidra.sh qemu/twin/stamplc-vuln-8MB.bin ota  # an image AND a focus
#
# The FIRST run does three one-time things, all cached afterward:
#   1. installs a private Ghidra 12.2 + a matching JDK under ~/ghidra (~700 MB),
#   2. builds firmware.elf with PlatformIO if you have not built it yet (so Ghidra reads the
#      real function names from the DWARF symbols), and
#   3. analyzes the image (a few minutes).
# Later runs — including a different function focus on the same image — are quick.
# ESP32-S3 Xtensa is NATIVE in Ghidra 12.2: no plugin, no manual load address. Output:
#   firmware-re/out/<image>.decompiled.c   — decompiled C for the security-relevant functions (or your focus)
#   firmware-re/out/<image>.functions.txt  — the full function index (name -> address)
# plus a Ghidra project under ~/ghidra/proj you can open in the Ghidra GUI to explore live.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/.." && pwd)"
GH="${GHIDRA_BASE:-$HOME/ghidra}"; GHD="$GH/ghidra_12.2_PUBLIC"; JDK="$GH/jdk"
GZIP=ghidra_12.2_PUBLIC_20261016.zip
GURL="https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/$GZIP"
JURL="https://api.adoptium.net/v3/binary/latest/25/ga/linux/x64/jdk/hotspot/normal/eclipse"
say(){ printf '\n>> %s\n' "$*"; }

# ---- one-time install (cached) ----
if [ ! -x "$JDK/bin/java" ]; then
  say "Installing a private JDK 25 for Ghidra (one-time)…"
  mkdir -p "$JDK"; curl -fsSL -o /tmp/_ghjdk.tgz "$JURL"
  tar xzf /tmp/_ghjdk.tgz -C "$JDK" --strip-components=1; rm -f /tmp/_ghjdk.tgz
fi
if [ ! -x "$GHD/support/analyzeHeadless" ]; then
  say "Installing Ghidra 12.2 (one-time, ~540 MB; Xtensa is built in)…"
  mkdir -p "$GH"; curl -fsSL -o /tmp/_ghidra.zip "$GURL"
  unzip -q /tmp/_ghidra.zip -d "$GH"; rm -f /tmp/_ghidra.zip
fi

# ---- pick the image + optional focus ----
IMG=""; FOCUS=""
if [ "${1:-}" ] && [ -f "$1" ]; then IMG="$1"; shift; fi
FOCUS="${1:-}"
if [ -z "$IMG" ]; then
  # default: the built firmware ELF (prefer the teaching build), with full DWARF symbols
  IMG="$(ls "$ROOT"/sbom/firmware/.pio/build/m5stamplc/firmware.elf \
            "$ROOT"/sbom/firmware/.pio/build/*/firmware.elf 2>/dev/null | head -1)"
  if [ -z "$IMG" ] && command -v pio >/dev/null 2>&1; then
    say "No firmware.elf yet — building it once so Ghidra gets real function names (pio run, a few minutes)…"
    if ( cd "$ROOT/sbom/firmware" && pio run -e m5stamplc ); then
      IMG="$ROOT/sbom/firmware/.pio/build/m5stamplc/firmware.elf"
    fi
  fi
  if [ -z "$IMG" ] || [ ! -f "$IMG" ]; then
    echo "No firmware.elf, and PlatformIO is not on PATH to build one."
    echo "Build it once, then re-run:   (cd sbom/firmware && pio run -e m5stamplc)"
    echo "Or point this tool at a flash image:   ./firmware-re/ghidra.sh qemu/twin/stamplc-vuln-8MB.bin"
    exit 0
  fi
fi
base="$(basename "$IMG")"; OUT="$ROOT/firmware-re/out"; mkdir -p "$OUT"

# per-image project so two images with the same basename (vuln vs secure firmware.elf) never collide
uid="$(printf '%s' "$(cd "$(dirname "$IMG")" && pwd)/$base" | cksum | cut -d' ' -f1)"
PROJ="$GH/proj"; mkdir -p "$PROJ"; PNAME="fw_$uid"; MARK="$PROJ/.$PNAME.done"

# analyze once, then just re-run the export on later calls (fast)
if [ -f "$MARK" ]; then
  say "Re-using the analyzed project for $base (focus='${FOCUS:-security-relevant}')…"
  MODE=(-process "$base" -noanalysis)
else
  if [[ "$IMG" == *.elf ]]; then MODE=(-import "$IMG"); else MODE=(-import "$IMG" -processor "Xtensa:LE:32:default"); fi
  say "First Ghidra analysis of $base (headless) — this takes a few minutes…"
fi
JAVA_HOME="$JDK" "$GHD/support/analyzeHeadless" "$PROJ" "$PNAME" "${MODE[@]}" \
  -scriptPath "$HERE" -postScript ghidra_export.java "$OUT/$base" "$FOCUS" \
  2>&1 | grep -iE 'Using Language|Import succeeded|GHIDRA_EXPORT|ERROR ' || true
touch "$MARK"

say "Done."
echo "  decompilation : firmware-re/out/${base}.decompiled.c"
echo "  function index: firmware-re/out/${base}.functions.txt"
echo "  Ghidra project: $PROJ  (open project '$PNAME' in the Ghidra GUI to explore interactively)"
