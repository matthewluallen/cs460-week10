#!/usr/bin/env bash
# to-sigrok.sh — reconstruct a protocol-faithful I2C VCD from the twin's recorded
# [BUS] capture and decode it headless with sigrok-cli (or open the VCD in PulseView).
# A protocol-timing view rebuilt from decoded transactions, not an analog acquisition.
# Tunables: CAP (input json), OUT (vcd path). Narrates each step.
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
CAP="${CAP:-$HERE/hooks/physical-bus-capture.json}"
OUT="${OUT:-$HERE/bus.vcd}"

step "I2C capture -> VCD -> sigrok decode"
say "The twin records bus transactions; we rebuild them into a logic-analyzer VCD so the same sigrok I2C decoder reads them. (A physical tap is only reference validation.)"
predict "Write down which device addresses you expect on this bus (e.g. 0x40 INA226, 0x48 LM75) before you see the decode."

step "1/2 — reconstruct the VCD"
say "Turn the decoded transactions into SCL/SDA edges at a synthetic I2C clock."
run python3 "$HERE/tools/bus_to_vcd.py" "$CAP" "$OUT"
observe "Note the transaction count, edge count and the address list printed. This is a protocol-timing reconstruction — it does NOT capture analog rise time or noise (a scope would)."

step "2/2 — decode it like a bench capture"
say "Run the stock sigrok I2C decoder; a faithful twin tap decodes identically to a physical LA."
run sigrok-cli -i "$OUT" -P i2c:scl=SCL:sda=SDA -A i2c   # Start/Address/Data/ACK/Stop
observe "Match each Start/Address/Data/ACK/Stop to a device + value (e.g. 0x48 LM75 -> 0x0134 = 30.8 C). Confirms what responded at each address; does NOT confirm the part is genuine."

finding "Record the addresses seen vs the addresses you predicted, and one decoded transaction traced to its device + value. Open $OUT in PulseView (add the I2C decoder on SCL/SDA) for the interactive waveform."
