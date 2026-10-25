#!/usr/bin/env python3
# twin_sampler.py — LIVE I2C sampler for the running StampLC QEMU twin.
# Polls the firmware's HTTP point API over the (emulated W5500) network stack and
# reconstructs the per-scan I2C transactions, so you get a live, growing VCD/JSON
# capture while the logic runs. Pair with a SLOWED twin (-icount shift=N,sleep=on)
# so each scan's bus burst arrives at a human pace instead of flooding.
#
#   # start a slowed twin first, e.g.:
#   #   TBD_W5_BIND=127.0.0.1 qemu-system-xtensa -L pc-bios -M esp32s3 -m 8M \
#   #     -icount shift=5,sleep=on -drive file=flash.bin,if=mtd,format=raw ...
#   python3 twin_sampler.py --samples 20 --interval 0.5 --out live
#   sigrok-cli -i live.vcd -P i2c:scl=SCL:sda=SDA -A i2c
import json, time, sys, argparse, urllib.request, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bus_to_vcd as b   # reuse the transaction -> I2C -> VCD encoder

# which point each polled device register carries (reconstruct the scan's bus reads)
READS = [
    (0x59, 'di0',        'read DI port (opto x8)'),
    (0x48, 'temp_c_x10', 'LM75 temp x10'),
    (0x40, 'vin_mv',     'INA226 Vin mV'),
    (0x40, 'current_ma', 'INA226 I mA'),
    (0x20, 'aio_ch1_mv', 'IO-exp CH1 mV'),
]

def fetch(url):
    with urllib.request.urlopen(url, timeout=5) as r:
        return json.loads(r.read().decode())

def points_to_txns(doc, t_us):
    pts = {p['n']: p for p in doc.get('pts', [])}
    def v(n, d=0):
        p = pts.get(n)
        try: return int(p['v'])
        except Exception: return d
    txns = []
    for i, (addr, pt, label) in enumerate(READS):
        txns.append({'t_us': t_us + i*5, 'dir': 'R', 'addr': addr,
                     'label': f"{label} = {v(pt)}", 'value': v(pt)})
    # a relay write closes the scan when the coil is set
    txns.append({'t_us': t_us + len(READS)*5, 'dir': 'W', 'addr': 0x59,
                 'label': f"relay0 = {v('relay0')}", 'value': v('relay0'), 'written': True})
    return txns

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1'); ap.add_argument('--port', type=int, default=8080)
    ap.add_argument('--samples', type=int, default=20); ap.add_argument('--interval', type=float, default=0.5)
    ap.add_argument('--out', default='live')
    a = ap.parse_args()
    base = f"http://{a.host}:{a.port}"
    allt, last_scan = [], None
    print(f"sampling {base}/api/points  x{a.samples} @ {a.interval}s")
    for i in range(a.samples):
        try:
            d = fetch(base + "/api/points")
        except Exception as e:
            print(f"  [{i}] poll failed: {e}"); time.sleep(a.interval); continue
        scan = d.get('scan'); mode = d.get('mode')
        t_us = (scan if isinstance(scan, int) else i) * 1000  # scan# as the time base
        txns = points_to_txns(d, t_us)
        allt.extend(txns)
        di = next((t['value'] for t in txns if t['addr'] == 0x59 and t['dir'] == 'R'), 0)
        rl = next((t['value'] for t in txns if t['addr'] == 0x59 and t['dir'] == 'W'), 0)
        print(f"  [{i}] scan={scan} mode={mode}  DI0={di} RELAY0={rl}  ({len(txns)} txns)")
        last_scan = scan
        time.sleep(a.interval)
    if not allt:
        print("no samples captured — is the twin up and on the network?"); sys.exit(1)
    w, placed = b.build(allt)
    b.emit_vcd(w, a.out + ".vcd")
    json.dump(placed, open(a.out + ".json", "w"), indent=2)
    print(f"captured {len(allt)} transactions -> {a.out}.vcd / {a.out}.json")

if __name__ == '__main__':
    main()
