#!/usr/bin/env python3
# trace_to_sigrok.py — VIRTUAL logic-analyzer tap for the StampLC QEMU twin.
#
# Reads the QEMU I2C controller's own observe trace (esp32_i2c_observe) — the byte-level
# bus traffic the emulated controller processes: address/data bytes, ACK/NACK, repeated
# start, STOP, with a virtual_ns timestamp — and draws the SDA/SCL waveform into a VCD.
# The same sigrok I2C decoder then reads it exactly as it reads a bench capture.
#
# Two ways to get a trace:
#   * LIVE, your own running twin (the lab path):  ./bus-analysis/tap.sh 6
#       (POST /bus/capture on the Studio server; it switches the trace event on through QMP
#        for N seconds while you act, then calls this converter)
#   * a throwaway twin (CI only):                  bus-analysis/tools/twin_tap.sh
#
#   python3 trace_to_sigrok.py trace.log --bus i2c0 --out bus                 # CI/compat (packed)
#   python3 trace_to_sigrok.py trace.log --bus i2c0 --out bus --timing virtual --field field.json
#   sigrok-cli -i bus.vcd -P i2c:scl=SCL:sda=SDA -A i2c
#
# What is observed vs drawn (say this in your finding):
#   OBSERVED  bytes, their order, ACK/NACK on writes, repeated start, STOP, reset — reported by the
#             emulated I2C controller, outside the firmware, so a malicious image cannot hide them.
#   INFERRED  the master's ACK/NACK on READ bytes (the trace has none; I2C requires ACK on every
#             byte but the last, which is NACKed) — marked "inferred" in the JSON.
#   DRAWN     the edges: bits at 400 kHz. --timing virtual places each transaction at its
#             virtual_ns offset; the emulated controller finishes a transfer faster than 400 kHz,
#             so a transaction that would overlap the previous one is pushed later (counted as
#             "nudged"). Not shown: rise times, noise, glitches, clock stretching.
#
# The older generic QEMU trace points (i2c_event/i2c_send/i2c_recv) are also accepted
# (--format generic or auto) for twin builds without esp32_i2c_observe: they carry no
# virtual time (host clock is used) and no bus path (the address decides the bus).
import argparse, bisect, collections, json, re, sys

OBS = re.compile(r'^(?:(\d+)@(\d+)\.(\d+):)?esp32_i2c_observe (.*)$')
KV = re.compile(r'(\w+)=(\S+)')
GEN = re.compile(r'^(?:(\d+)@(\d+)\.(\d+):)?i2c_(event|send|recv|send_async) '
                 r'(\w+)\(addr:0x([0-9a-fA-F]{2})\)(?: data:0x([0-9a-fA-F]{2}))?')

# Which bus an address lives on (twin machine model; used only for the generic format).
BUS_OF_ADDR = {0x20: "i2c0", 0x32: "i2c0", 0x40: "i2c0", 0x43: "i2c0", 0x48: "i2c0", 0x59: "i2c0",
               0x77: "i2c1", 0x5A: "i2c1", 0x58: "i2c1", 0x57: "i2c1", 0x29: "i2c1", 0x70: "i2c1"}
RESET_EVENTS = {"reset", "fsm_reset", "peripheral-reset"}
STOP_EVENTS = {"stop", "ack_mismatch_stop"}
IGNORED_EVENTS = {"tx_wait", "rx_wait", "invalid_write_count", "invalid_opcode",
                  "observation-fence", "end_command", "rstart_command"}


def _wall(m):
    if m.group(2) is None:
        return None
    return int(m.group(2)) + int(m.group(3)) / 1e6


def parse_lines(lines, bus="i2c0", fmt="auto"):
    """Parse trace lines into normalised events for one bus (or 'all').

    Returns (events, info). Each event: {seq, ns, wall, ev, addr, dir, data, nack, path}.
    ns is virtual time (observe format) or host time in ns (generic format)."""
    want = None if bus == "all" else "/machine/soc/" + bus
    evs, info = [], collections.Counter()
    gen_state = {}                                  # addr -> current direction (generic)
    for raw in lines:
        line = raw.rstrip("\n")
        if fmt in ("auto", "observe") and "esp32_i2c_observe" in line:
            m = OBS.match(line)
            if not m:
                info["malformed"] += 1
                continue
            d = dict(KV.findall(m.group(4)))
            if want and d.get("path") != want:
                info["other_bus"] += 1
                continue
            ev = d.get("event", "?")
            info["event:" + ev] += 1
            try:
                evs.append({"seq": int(d.get("seq", 0)), "ns": int(d.get("virtual_ns", 0)),
                            "wall": _wall(m), "ev": ev, "addr": int(d.get("address", -1)),
                            "dir": int(d.get("direction", 0)), "data": int(d.get("data", -1)),
                            "nack": int(d.get("nack", -1)), "path": d.get("path", ""),
                            "token": d.get("controller", "")})
            except ValueError:
                info["malformed"] += 1
            continue
        if fmt in ("auto", "generic"):
            m = GEN.match(line)
            if not m:
                continue
            kind, word, addr = m.group(4), m.group(5), int(m.group(6), 16)
            path = "/machine/soc/" + BUS_OF_ADDR.get(addr, "i2c?")
            if want and path != want:
                info["other_bus"] += 1
                continue
            wall = _wall(m)
            ns = int(wall * 1e9) if wall is not None else len(evs) * 1000
            base = {"seq": len(evs) + 1, "ns": ns, "wall": wall, "addr": addr, "path": path, "token": "generic"}
            if kind == "event":
                if word.startswith("start"):
                    dr = 1 if word == "start_async" or word == "start_recv" else 0
                    gen_state[addr] = dr
                    evs.append(dict(base, ev="address", dir=dr, data=(addr << 1) | dr, nack=0))
                elif word in ("finish", "stop"):
                    evs.append(dict(base, ev="stop", dir=0, data=-1, nack=-1))
                elif word == "nack":
                    info["generic_nack"] += 1
                info["generic:" + word] += 1
            elif kind in ("send", "send_async"):
                evs.append(dict(base, ev="write", dir=0, data=int(m.group(7), 16), nack=0))
            elif kind == "recv":
                evs.append(dict(base, ev="read", dir=1, data=int(m.group(7), 16), nack=-1))
            info["format_generic"] = 1
    evs.sort(key=lambda e: (e["ns"], e["seq"]))
    return evs, info


def parse(path, bus):
    """Backward-compatible entry point (parity_test.py): events for one bus."""
    with open(path, errors="replace") as f:
        return parse_lines(f, bus)[0]


def frames_from(evs):
    """Group events into START..STOP frames made of segments (repeated start = new segment)."""
    frames, cur = [], None

    def close(end):
        nonlocal cur
        if cur:
            cur["end"] = end
            frames.append(cur)
            cur = None
    for e in evs:
        ev = e["ev"]
        if ev in RESET_EVENTS:
            close("reset")
            continue
        if ev == "address":
            seg = {"addr": e["addr"] if e["addr"] >= 0 else (e["data"] >> 1),
                   "dir": "R" if e["dir"] == 1 else "W", "addr_nack": 1 if e["nack"] == 1 else 0,
                   "bytes": [], "acks": [], "inferred": []}
            if cur is None:
                cur = {"ns": e["ns"], "wall": e["wall"], "segments": [], "end": None}
            cur["segments"].append(seg)
            continue
        if ev in ("write", "read"):
            if cur is None or not cur["segments"] or e["data"] < 0:
                continue
            seg = cur["segments"][-1]
            seg["bytes"].append(e["data"] & 0xFF)
            if ev == "write":
                seg["acks"].append(1 if e["nack"] == 1 else 0)
                seg["inferred"].append(False)
            else:
                seg["acks"].append(None)             # filled below: master ACK, last NACK
                seg["inferred"].append(True)
            continue
        if ev in STOP_EVENTS:
            close("stop")
    close("open")
    for fr in frames:                                 # master ACK/NACK on reads (inferred)
        for seg in fr["segments"]:
            if seg["dir"] == "R":
                n = len(seg["bytes"])
                seg["acks"] = [0 if i < n - 1 else 1 for i in range(n)]
    return frames


class Wave:
    """Two-wire waveform at integer ticks; H = half a bit period."""
    def __init__(s, half):
        s.H = half; s.ev = []; s.scl = 1; s.sda = 1; s.t = 0

    def _set(s, sc, sd):
        if sc != s.scl or sd != s.sda:
            s.ev.append((s.t, sc, sd)); s.scl, s.sda = sc, sd

    def idle_to(s, t):
        s._set(1, 1); s.t = max(s.t, t)

    def start(s):
        s._set(1, 1); s.t += s.H; s._set(1, 0); s.t += s.H

    def rstart(s):
        s._set(0, 1); s.t += s.H; s._set(1, 1); s.t += s.H; s._set(1, 0); s.t += s.H

    def _bit(s, b):
        s._set(0, b); s.t += s.H; s._set(1, b); s.t += s.H

    def byte(s, v, ackbit):
        for i in range(7, -1, -1):
            s._bit((v >> i) & 1)
        s._bit(ackbit)

    def stop(s):
        s._set(0, 0); s.t += s.H; s._set(1, 0); s.t += s.H; s._set(1, 1); s.t += s.H


def draw(frames, timing="packed", max_gap_ms=None):
    """Draw frames. Returns (wave, ts_ns, stats); each frame gets 't' (tick of its START)."""
    if timing == "virtual":
        ts_ns, half = 250, 5                         # 4 MHz sample clock, 400 kHz bits
    else:
        ts_ns, half = 10, 125                        # legacy packed layout (CI parity)
    bit = 2 * half
    w = Wave(half)
    stats = {"nudged": 0, "compressed": 0, "timing": timing, "timescale_ns": ts_ns}
    t0 = frames[0]["ns"] if frames else 0
    max_gap = int(max_gap_ms * 1e6 / ts_ns) if max_gap_ms else None
    shift = 0                                       # ticks removed by gap compression so far
    for fr in frames:
        if timing == "virtual":
            target = (fr["ns"] - t0) // ts_ns - shift
            earliest = w.t + bit
            if max_gap is not None and target - w.t > max_gap:
                shift += target - (w.t + max_gap)
                target = w.t + max_gap
                stats["compressed"] += 1
            if target < earliest:
                stats["nudged"] += 1
                target = earliest
            w.idle_to(target)
        else:
            w.idle_to(w.t + bit)
        fr["t"] = w.t
        for i, seg in enumerate(fr["segments"]):
            (w.start if i == 0 else w.rstart)()
            w.byte((seg["addr"] << 1) | (1 if seg["dir"] == "R" else 0), seg["addr_nack"])
            for v, a in zip(seg["bytes"], seg["acks"]):
                w.byte(v, a)
        w.stop()
    w.idle_to(w.t + bit)
    stats["ticks"] = w.t
    stats["span_s"] = round(w.t * ts_ns / 1e9, 4)
    return w, ts_ns, stats


def field_channels(frames, field, nbits=4):
    """field: {"di0": byte at start, "writes": [{"t": wall, "di": byte}, ...]}.
    Each /field write is placed at the first bus frame logged at or after it (host clock)."""
    if not field:
        return []
    walls = [(fr["wall"], fr["t"]) for fr in frames if fr.get("wall") is not None]
    walls.sort()
    keys = [w for w, _ in walls]
    changes = []
    di = int(field.get("di_start", 0))
    for wr in field.get("writes", []):
        i = bisect.bisect_left(keys, float(wr["t"]))
        if i >= len(walls):
            continue
        changes.append((max(0, walls[i][1] - 1), int(wr["di"]) & 0xFF))
    out = []
    for b in range(nbits):
        lv = (di >> b) & 1
        ev = [(0, lv)]
        for t, v in changes:
            nv = (v >> b) & 1
            if nv != ev[-1][1]:
                ev.append((t, nv))
        out.append(("IN%d" % (b + 1), ev))
    return out


def build(evs):
    """Backward-compatible entry point (parity_test.py): packed wave + flat transactions."""
    frames = frames_from(evs)
    w, ts_ns, _ = draw(frames, "packed")
    return w, transactions(frames, ts_ns)


def emit_vcd(w, path, ts_ns=10, extra=()):
    ids = ["!", '"', "#", "$", "%", "&", "'", "("]
    out = ["$timescale %dns $end" % ts_ns, "$scope module i2c $end",
           "$var wire 1 ! SCL $end", '$var wire 1 " SDA $end']
    for k, (name, _) in enumerate(extra):
        out.append("$var wire 1 %s %s $end" % (ids[2 + k], name))
    out += ["$upscope $end", "$enddefinitions $end", "#0", "1!", '1"']
    timeline = collections.defaultdict(list)
    for t, sc, sd in w.ev:
        timeline[t].append(("i2c", sc, sd))
    for k, (_, ev) in enumerate(extra):
        out.append("%d%s" % (ev[0][1], ids[2 + k]))
        for t, v in ev[1:]:
            timeline[t].append(("x", ids[2 + k], v))
    last = {"!": 1, '"': 1}
    for t in sorted(timeline):
        ch = []
        for item in timeline[t]:
            if item[0] == "i2c":
                _, sc, sd = item
                if sc != last["!"]: ch.append("%d!" % sc); last["!"] = sc
                if sd != last['"']: ch.append('%d"' % sd); last['"'] = sd
            else:
                ch.append("%d%s" % (item[2], item[1]))
        if ch:
            out.append("#%d" % t); out += ch
    out.append("#%d" % (w.t + 1))
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def transactions(frames, ts_ns):
    """Flat transaction list (one per segment) — the shape i2c-analyzer.html loads."""
    out = []
    for fr in frames:
        for i, seg in enumerate(fr["segments"]):
            out.append({"addr": seg["addr"], "dir": seg["dir"], "bytes": seg["bytes"],
                        "nack": seg["addr_nack"], "ns": fr["ns"],
                        "t_us": round(fr["t"] * ts_ns / 1000.0, 3) if "t" in fr else None,
                        "wall": fr["wall"], "repeated_start": i > 0, "end": fr["end"],
                        "read_acks_inferred": seg["dir"] == "R"})
    return out


RELAY_NAMES = ["RELAY 1 (relay0)", "RELAY 2 (relay1)", "RELAY 3 (relay2)", "RELAY 4 (relay3)"]
INPUT_NAMES = ["IN1 (di0)", "IN2 (di1)", "IN3 (di2)", "IN4 (di3)"]


def summarize(frames, info, stats, ts_ns):
    """What a student looks for: AW9523 (0x59) relay writes and input reads, with times."""
    by_addr = collections.Counter()
    relay_writes, input_reads = [], []
    t0 = frames[0]["ns"] if frames else 0
    pointer = None
    for fr in frames:
        for seg in fr["segments"]:
            by_addr["0x%02x %s" % (seg["addr"], seg["dir"])] += 1
            if seg["addr"] != 0x59:
                continue
            if seg["dir"] == "W" and seg["bytes"]:
                pointer = seg["bytes"][0]
                if seg["bytes"][0] == 0x02 and len(seg["bytes"]) >= 2:
                    v = seg["bytes"][1]
                    relay_writes.append({"t_ms": round((fr["ns"] - t0) / 1e6, 3), "virtual_ns": fr["ns"],
                                         "drawn_us": round(fr.get("t", 0) * ts_ns / 1000.0, 3),
                                         "value": "0x%02x" % v,
                                         "on": [RELAY_NAMES[b] for b in range(4) if v >> b & 1]})
            elif seg["dir"] == "R" and seg["bytes"] and pointer == 0x00:
                v = seg["bytes"][0]
                input_reads.append({"t_ms": round((fr["ns"] - t0) / 1e6, 3), "value": "0x%02x" % v,
                                    "on": [INPUT_NAMES[b - 4] for b in range(4, 8) if v >> b & 1]})

    def changes(rows):
        out, last = [], None
        for r in rows:
            if r["value"] != last:
                out.append(r); last = r["value"]
        return out
    warnings = []
    if not frames:
        warnings.append("no I2C transactions in the trace — the trace event was not active, or the "
                        "twin build has no esp32_i2c_observe event (see bus-analysis/README.md)")
    if stats.get("compressed"):
        warnings.append("%d idle gaps longer than the limit were shortened in the drawing; use the "
                        "virtual_ns column in the JSON for timing" % stats["compressed"])
    return {"events": sum(v for k, v in info.items() if k.startswith("event:")) or None,
            "frames": len(frames), "by_address": dict(by_addr.most_common()),
            "relay_writes_0x59_reg02": len(relay_writes),
            "relay_write_changes": changes(relay_writes),
            "input_read_changes_0x59_reg00": changes(input_reads),
            "drawing": stats, "warnings": warnings,
            "fidelity": "Bytes and order are OBSERVED (emulated I2C controller, outside the firmware). "
                        "Edges are DRAWN at 400 kHz; read ACK/NACK is INFERRED. Not shown: analog "
                        "detail, clock stretching, absent-address probes."}


def convert(lines, bus="i2c0", out="bus", timing="packed", field=None, max_gap_ms=None, fmt="auto"):
    evs, info = parse_lines(lines, bus, fmt)
    frames = frames_from(evs)
    w, ts_ns, stats = draw(frames, timing, max_gap_ms)
    extra = field_channels(frames, field) if field else []
    emit_vcd(w, out + ".vcd", ts_ns, extra)
    txns = transactions(frames, ts_ns)
    with open(out + ".json", "w") as f:
        json.dump(txns, f, indent=1)
    summ = summarize(frames, info, stats, ts_ns)
    summ["events_parsed"] = len(evs)
    summ["edges"] = len(w.ev)
    summ["channels"] = ["SCL", "SDA"] + [n for n, _ in extra]
    return summ, txns


def main():
    ap = argparse.ArgumentParser(description=__doc__ if False else "QEMU I2C observe trace -> VCD + JSON")
    ap.add_argument("trace")
    ap.add_argument("--bus", default="i2c1", help="i2c0 (relays/DI/sensors) or i2c1 (Grove PORT.A)")
    ap.add_argument("--out", default="bus")
    ap.add_argument("--timing", choices=("packed", "virtual"), default="packed",
                    help="packed: back-to-back (CI parity); virtual: place transactions at virtual_ns")
    ap.add_argument("--field", help="JSON {di_start, writes:[{t, di}]} from the Studio server -> IN1..IN4 channels")
    ap.add_argument("--max-gap-ms", type=float, default=None, help="shorten idle gaps longer than this")
    ap.add_argument("--format", choices=("auto", "observe", "generic", "qemu-core"), default="auto",
                    help="observe = esp32_i2c_observe (released twin); generic/qemu-core = i2c_event/send/recv")
    ap.add_argument("--max", type=int, default=0, help="limit transactions (frames)")
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=None, help="keep only frames touching this address")
    ap.add_argument("--summary", action="store_true", help="print the JSON summary")
    a = ap.parse_args()
    if a.format == "qemu-core":
        a.format = "generic"
    with open(a.trace, errors="replace") as f:
        lines = f.readlines()
    if a.addr is not None or a.max:
        evs, info = parse_lines(lines, a.bus, a.format)
        frames = frames_from(evs)
        if a.addr is not None:
            frames = [fr for fr in frames if any(s["addr"] == a.addr for s in fr["segments"])]
        if a.max:
            frames = frames[:a.max]
        w, ts_ns, stats = draw(frames, a.timing, a.max_gap_ms)
        emit_vcd(w, a.out + ".vcd", ts_ns)
        txns = transactions(frames, ts_ns)
        json.dump(txns, open(a.out + ".json", "w"), indent=1)
        summ = summarize(frames, info, stats, ts_ns)
        summ["events_parsed"] = len(evs)
    else:
        field = json.load(open(a.field)) if a.field else None
        summ, txns = convert(lines, a.bus, a.out, a.timing, field, a.max_gap_ms, a.format)
    print("bus %s: %d events -> %d transactions -> %s.vcd (%d edges)"
          % (a.bus, summ.get("events_parsed", 0), len(txns), a.out, summ.get("edges", 0)))
    c = collections.Counter((t["addr"], t["dir"]) for t in txns)
    for (ad, dr), n in c.most_common(10):
        print("  0x%02x %s: %d" % (ad, dr, n))
    for wmsg in summ["warnings"]:
        print("  WARNING: " + wmsg)
    if a.summary:
        print(json.dumps(summ, indent=1))
    return 3 if not txns else 0


if __name__ == "__main__":
    sys.exit(main())
