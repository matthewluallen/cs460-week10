#!/usr/bin/env python3
# bus_to_vcd.py — reconstruct a sigrok/PulseView-openable I2C logic-analyzer capture
# (VCD) from the firmware's [BUS] diagnostic log (a tbd.diagnostics/v1 capture, or any
# list of "[BUS] <ts> <R|W> 0x<addr> <label>[=value]" lines).
#
# IMPORTANT (fidelity): QEMU models I2C at the TRANSACTION level — there are no analog
# SDA/SCL edges. This tool RECONSTRUCTS a protocol-faithful waveform from each decoded
# transaction at a synthetic I2C clock, so PulseView's I2C decoder re-annotates
# Start/Address/Data/ACK/Stop. It is a protocol-timing view, not an analog acquisition.
#
#   python3 bus_to_vcd.py <capture.json|-> [out.vcd] [transactions.json]
#   # then:  sigrok-cli -i out.vcd -P i2c:scl=SCL:sda=SDA -A i2c
import json, re, sys

TS_NS = 10               # VCD timescale: 10 ns/unit
BIT_NS = 2500            # 400 kHz I2C -> 2.5 us bit period
U = BIT_NS // TS_NS      # ticks per bit (250)
H = U // 2               # half bit (125)

def load_records(path):
    txt = sys.stdin.read() if path == '-' else open(path).read()
    try:
        d = json.loads(txt)
    except json.JSONDecodeError:
        # plain log lines
        return [l for l in txt.splitlines() if '[BUS]' in l]
    recs = d.get('records', d if isinstance(d, list) else [])
    out = []
    for r in recs:
        raw = r.get('raw') if isinstance(r, dict) else r
        try:
            inner = json.loads(raw) if isinstance(raw, str) else raw
        except Exception:
            inner = None
        if isinstance(inner, dict) and inner.get('ascii_preview'):
            out.append((inner.get('enqueue_us'), inner['ascii_preview']))
        elif isinstance(r, str):
            out.append((None, r))
    return out

LINE = re.compile(r'\[BUS\]\s+(\d+)\s+([RW])\s+0x([0-9a-fA-F]+)\s+(.*)')

def parse(records):
    txns = []
    for item in records:
        us, line = item if isinstance(item, tuple) else (None, item)
        m = LINE.search(line)
        if not m:
            continue
        ts, rw, addr, label = m.groups()
        addr = int(addr, 16)
        val = None
        mv = re.search(r'=\s*(-?\d+)', label)
        if mv:
            val = int(mv.group(1))
        txns.append({'t_us': us if us is not None else int(ts),
                     'dir': 'R' if rw == 'R' else 'W',
                     'addr': addr, 'label': label.strip().rstrip('.'),
                     'value': val})
    txns.sort(key=lambda x: x['t_us'])
    return txns

def data_bytes(txn):
    """Reconstruct the data byte(s) for the transaction from its decoded value."""
    v = txn['value']
    a = txn['addr']
    if v is None:
        return [0x00]                      # e.g. idle DI poll
    if a in (0x40, 0x48, 0x20):            # 16-bit sensor registers
        v &= 0xFFFF
        return [(v >> 8) & 0xFF, v & 0xFF]
    return [v & 0xFF]

class Wave:
    """Accumulates (tick, scl, sda) transitions; bus idles high."""
    def __init__(self):
        self.ev = []; self.scl = 1; self.sda = 1; self.t = 0
    def set(self, t, scl, sda):
        if scl != self.scl or sda != self.sda:
            self.ev.append((t, scl, sda)); self.scl, self.sda = scl, sda
    def start_at(self, t):
        self.set(t, 1, 1); self.set(t + H, 1, 0); return t + U       # START: SDA 1->0, SCL high
    def bit(self, t, b):
        self.set(t, 0, b); self.set(t + H, 1, b); return t + U       # clock one bit
    def byte(self, t, val):
        for i in range(7, -1, -1):
            t = self.bit(t, (val >> i) & 1)
        t = self.bit(t, 0)                                           # ACK (receiver pulls low)
        return t
    def stop_at(self, t):
        self.set(t, 0, 0); self.set(t + H, 1, 0); self.set(t + U, 1, 1); return t + U  # STOP

def build(txns):
    w = Wave()
    placed = []
    # lay transactions at their real µs offset (10ns units), but collapse huge idle gaps
    base = txns[0]['t_us'] if txns else 0
    cursor = 0
    for txn in txns:
        t0 = (txn['t_us'] - base) * 100          # µs -> 10ns units
        if t0 < cursor + U:                      # avoid overlap: pack sequentially
            t0 = cursor + 4 * U
        t = w.start_at(t0)
        t = w.byte(t, (txn['addr'] << 1) | (1 if txn['dir'] == 'R' else 0))
        for by in data_bytes(txn):
            t = w.byte(t, by)
        t = w.stop_at(t)
        cursor = t
        placed.append({**txn, 'vcd_start_ns': t0 * TS_NS})
    return w, placed

def emit_vcd(w, path):
    lines = [f"$timescale {TS_NS}ns $end",
             "$scope module i2c_bus $end",
             "$var wire 1 ! SCL $end",
             "$var wire 1 \" SDA $end",
             "$upscope $end", "$enddefinitions $end",
             "#0", "1!", "1\""]           # idle high
    last = {'!': 1, '"': 1}
    for t, scl, sda in w.ev:
        chunk = [f"#{t}"]
        if scl != last['!']:
            chunk.append(f"{scl}!"); last['!'] = scl
        if sda != last['"']:
            chunk.append(f"{sda}\""); last['"'] = sda
        if len(chunk) > 1:
            lines.extend(chunk)
    open(path, 'w').write("\n".join(lines) + "\n")

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else '-'
    out = sys.argv[2] if len(sys.argv) > 2 else 'bus.vcd'
    txj = sys.argv[3] if len(sys.argv) > 3 else None
    txns = parse(load_records(src))
    if not txns:
        print("no [BUS] transactions found", file=sys.stderr); sys.exit(1)
    w, placed = build(txns)
    emit_vcd(w, out)
    if txj:
        json.dump(placed, open(txj, 'w'), indent=2)
    print(f"{len(txns)} transactions -> {out} ({len(w.ev)} edges)")
    addrs = sorted(set(t['addr'] for t in txns))
    print("addresses:", " ".join(f"0x{a:02x}" for a in addrs))

if __name__ == '__main__':
    main()
