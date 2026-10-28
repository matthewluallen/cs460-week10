#!/usr/bin/env bash
# live-sample.sh — live-capture the running twin's per-scan I2C transactions into a
# growing VCD + JSON, then decode with sigrok-cli. Run the twin SLOWED first
# (see tools/twin_tap.sh: qemu ... -icount shift=5,sleep=on) so the bus is human-paced.
# Tunables: SAMPLES, INTERVAL (s), OUT (basename). Narrates each step.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"            # bus-analysis/
source "$HERE/../tools/teach.sh" 2>/dev/null || true
declare -F step    >/dev/null 2>&1 || step()    { printf '\n==== %s ====\n' "$*"; }
declare -F say     >/dev/null 2>&1 || say()     { printf '>  %s\n' "$*"; }
declare -F run     >/dev/null 2>&1 || run()     { printf '   $ %s\n' "$*"; "$@"; }
declare -F observe >/dev/null 2>&1 || observe() { printf '-> %s\n' "$*"; }
declare -F predict >/dev/null 2>&1 || predict() { printf '?  PREDICT FIRST: %s\n' "$*"; }
declare -F finding >/dev/null 2>&1 || finding() { printf 'FINDING: %s\n' "$*"; }
declare -F note    >/dev/null 2>&1 || note()    { printf '   %s\n' "$*"; }
SAMPLES="${SAMPLES:-20}"
INTERVAL="${INTERVAL:-0.5}"
OUT="${OUT:-live}"

step "Live I2C sample from the running twin"
say "Your test environment is the QEMU twin. We poll its /api/points over the emulated W5500 and rebuild the per-scan I2C transactions into a LIVE capture while the logic runs."
note "Start the twin SLOWED first (tools/twin_tap.sh launches it with -icount shift=5,sleep=on) so the bus cadence is legible instead of thousands of transactions/sec."
predict "Write down which addresses should appear each scan, and whether a value you expect to change (e.g. a sensor) actually moves between samples."

step "1/2 — sample the live bus"
say "Collect $SAMPLES samples at ${INTERVAL}s spacing into $OUT.vcd + $OUT.json."
run python3 "$HERE/tools/twin_sampler.py" --samples "$SAMPLES" --interval "$INTERVAL" --out "$OUT"
observe "This is a live reconstruction from the twin's transactions — shows WHAT the bus carried over time; it does NOT add analog detail and is not a physical probe."

step "2/2 — decode the captured window"
say "Decode the growing capture with the same sigrok I2C decoder used for recorded files."
run sigrok-cli -i "$OUT.vcd" -P i2c:scl=SCL:sda=SDA -A i2c
observe "Compare values across samples: changing values are fresh acquisitions; identical values across a quiet window are expected, not frozen hardware."

finding "Record the addresses seen live vs predicted, and one register that changed (or did not) across samples, with the reason. Freeze/stage with QMP stop/cont + qom-set while stopped to drive a value and watch the next scan react; Load $OUT.json in i2c-analyzer.html for the lane view."
