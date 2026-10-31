#!/usr/bin/env bash
# tap.sh — record YOUR running twin's I²C bus for N seconds (default 6), live.
#
#   ./bus-analysis/tap.sh            # 6 s on i2c0 (relays/inputs/sensors)
#   ./bus-analysis/tap.sh 8 i2c1     # 8 s on the Grove bus (PORT.A)
#
# Needs the twin + Studio server:  ./qemu/studio.sh   (same twin the Studio, console and Modbus use).
# While it records, change an input — e.g. press IN1 (di0) at the chip:
#   curl -s -X POST 'localhost:8080/field?bit=0&v=1'; sleep 1; curl -s -X POST 'localhost:8080/field?bit=0&v=0'
#
# The Studio server switches QEMU's I²C controller trace on through QMP for N s, then off, and
# converts it (tools/trace_to_sigrok.py). You get, in bus-analysis/out/:
#   <id>.sr          open in PulseView (Add decoder: I²C, SCL=SCL, SDA=SDA)
#   <id>.vcd         the same waveform as text (any VCD viewer)
#   <id>.decode.txt  sigrok-cli's I²C decode, with sample numbers (÷ 4 MHz = seconds)
#   <id>.json        one row per transaction (address, R/W, bytes, virtual time)
#   <id>.manifest.json  what was captured, from which twin/firmware, and the /field writes
#   <id>.studio.json    loads into the Studio's Bus monitor (Open capture)
set -euo pipefail
SECS="${1:-6}"; BUS="${2:-i2c0}"
PORT="${BRIDGE_PORT:-8080}"; URL="http://127.0.0.1:$PORT"
if ! curl -s -o /dev/null --max-time 3 "$URL/logic_studio.html"; then
  echo "The Studio server is not answering on $URL — start the twin + server first:  ./qemu/studio.sh"
  exit 1
fi
echo "Recording $BUS for ${SECS} s on your running twin — act now (press IN1, start the motor, …)"
OUT="$(mktemp)"; trap 'rm -f "$OUT"' EXIT
curl -s --max-time $((SECS + 200)) -X POST "$URL/bus/capture?seconds=$SECS&bus=$BUS" -o "$OUT" || {
  echo "capture request failed"; exit 1; }
python3 - "$OUT" "$URL" <<'PY'
import json, sys
j = json.load(open(sys.argv[1])); url = sys.argv[2]
if not j.get("ok"):
    print("FAILED:", j.get("error")); sys.exit(1)
s = j["summary"]
print("captured %s  %s s  %s  firmware ELF %s  (%s profile)" % (
    j["captured"]["start_local"], j["seconds"], j["bus"],
    ((j.get("firmware") or {}).get("elf_sha256") or "?")[:16], j["twin"].get("profile")))
print("transactions: %d   by address: %s" % (s["frames"], ", ".join("%s×%d" % kv for kv in s["by_address"].items())))
print("RELAY byte written to 0x59 register 0x02 (bit0 = RELAY 1 / relay0 … bit3 = RELAY 4 / relay3):")
for c in s["relay_write_changes"]:
    print("   t=%9.3f ms   0x59 <- [0x02, %s]   on: %s" % (c["t_ms"], c["value"], ", ".join(c["on"]) or "none"))
print("inputs read from 0x59 register 0x00 (bit4 = IN1 / di0 … bit7 = IN4 / di3):")
for c in s["input_read_changes_0x59_reg00"]:
    print("   t=%9.3f ms   0x59 -> %s   on: %s" % (c["t_ms"], c["value"], ", ".join(c["on"]) or "none"))
f = j["field"]
print("/field writes during the capture: %d   relays latch %s -> %s" % (len(f["writes"]), f["relays_start"], f["relays_end"]))
sg = j.get("sigrok") or {}
if sg.get("ok"):
    print("sigrok-cli decode: %s" % ", ".join("%s×%d" % kv for kv in sg["addresses"].items()))
    for c in sg.get("relay_write_changes_0x59_reg02", []):
        print("   sigrok: write 0x59 [0x02, %s] at %.6f s" % (c["value"], c["t_s"]))
else:
    print("sigrok-cli decode: not run (%s)" % sg.get("error"))
for w in s.get("warnings", []):
    print("WARNING:", w)
print("files (%s/):" % j["repo_path"])
for k, v in j["files"].items():
    print("   %-9s %s     (%s%s)" % (k, v, url, j["download"][k]))
print(s["fidelity"])
PY
