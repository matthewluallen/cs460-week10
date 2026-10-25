#!/usr/bin/env python3
# trace_to_sigrok.py — VIRTUAL logic-analyzer tap for the StampLC QEMU twin.
#
# Reads the QEMU I2C controller's own observe trace (esp32_i2c_observe) — the
# complete byte-level bus traffic the controller processes: START/address/repeated-
# start/data/ACK-NACK/STOP, with a deterministic virtual_ns timestamp — and
# reconstructs the SDA/SCL waveform into a VCD. This is the emulated equivalent of
# clipping a logic analyzer onto the physical bus: the SAME sigrok I2C decoder gives
# the SAME answers on this capture as on a real probe (a logic analyzer samples
# digital thresholds, which the protocol fully determines).
#
#   qemu-system-xtensa ... --trace enable=esp32_i2c_observe -D trace.log
#   python3 trace_to_sigrok.py trace.log --bus i2c1 --out bus
#   sigrok-cli -i bus.vcd -P i2c:scl=SCL:sda=SDA -A i2c   # identical to a bench capture
import re, sys, json, argparse

TS_NS = 10; BIT = 2500 // TS_NS; H = BIT // 2          # synthetic 400 kHz intra-frame clock
FIELD = re.compile(r'(\w+)=(-?\d+|[\w/]+)')

def parse(path, bus):
    want = "/machine/soc/" + bus
    evs = []
    for line in open(path):
        if "esp32_i2c_observe" not in line: continue
        d = dict(FIELD.findall(line))
        if d.get("path") != want: continue
        evs.append({"seq": int(d.get("seq", 0)), "ns": int(d.get("virtual_ns", 0)),
                    "ev": d.get("event"), "addr": int(d.get("address", -1)),
                    "dir": int(d.get("direction", 0)), "data": int(d.get("data", -1)),
                    "nack": int(d.get("nack", 0))})
    evs.sort(key=lambda e: (e["ns"], e["seq"]))
    return evs

class Wave:
    def __init__(s): s.ev=[]; s.scl=1; s.sda=1; s.t=0
    def _set(s,sc,sd):
        if sc!=s.scl or sd!=s.sda: s.ev.append((s.t,sc,sd)); s.scl,s.sda=sc,sd
    def idle(s,n=BIT): s._set(1,1); s.t+=n
    def start(s): s._set(1,1); s.t+=H; s._set(1,0); s.t+=H          # SDA 1->0, SCL high
    def rstart(s): s._set(0,1); s.t+=H; s._set(1,1); s.t+=H; s._set(1,0); s.t+=H  # release, then start
    def _bit(s,b): s._set(0,b); s.t+=H; s._set(1,b); s.t+=H
    def byte(s,v,ackbit):
        for i in range(7,-1,-1): s._bit((v>>i)&1)
        s._bit(ackbit)                                             # 9th bit: 0=ACK, 1=NACK
    def stop(s): s._set(0,0); s.t+=H; s._set(1,0); s.t+=H; s._set(1,1); s.t+=H

def build(evs):
    w=Wave(); txns=[]; cur=None; inx=False
    def close():
        nonlocal cur
        if cur: txns.append(cur); cur=None
    for e in evs:
        ev=e["ev"]
        if ev in ("reset",):
            if inx: w.stop(); inx=False
            close(); w.idle(); continue
        if ev=="address":
            if not inx: w.start(); inx=True; w.idle(0)
            else: w.rstart()
            close()
            cur={"addr":e["addr"],"dir":"R" if e["dir"] else "W","bytes":[],"ns":e["ns"],"nack":e["nack"]}
            w.byte((e["addr"]<<1)|(1 if e["dir"] else 0), e["nack"])
        elif ev in ("write","read"):
            if cur is not None and e["data"]>=0:
                cur["bytes"].append(e["data"]); w.byte(e["data"], e["nack"] if e["nack"]>=0 else 0)
        elif ev in ("stop","ack_mismatch_stop"):
            if inx: w.stop(); inx=False
            close(); w.idle()
    if inx: w.stop()
    close()
    return w, txns

def emit_vcd(w, path):
    out=[f"$timescale {TS_NS}ns $end","$scope module i2c $end",
         "$var wire 1 ! SCL $end",'$var wire 1 " SDA $end',
         "$upscope $end","$enddefinitions $end","#0","1!",'1"']
    last={"!":1,'"':1}
    for t,sc,sd in w.ev:
        ch=[f"#{t}"]
        if sc!=last["!"]: ch.append(f"{sc}!"); last["!"]=sc
        if sd!=last['"']: ch.append(f'{sd}"'); last['"']=sd
        if len(ch)>1: out+=ch
    open(path,"w").write("\n".join(out)+"\n")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("trace"); ap.add_argument("--bus",default="i2c1")
    ap.add_argument("--out",default="bus"); ap.add_argument("--max",type=int,default=0,help="limit transactions")
    ap.add_argument("--addr",type=lambda x:int(x,0),default=None,help="filter to one 7-bit address")
    a=ap.parse_args()
    evs=parse(a.trace,a.bus)
    if a.addr is not None:
        keep=set(); # keep events belonging to transactions touching addr
        evs=[e for e in evs if e["addr"]==a.addr or e["ev"] in ("stop","ack_mismatch_stop","reset")]
    w,txns=build(evs)
    if a.max and len(txns)>a.max:
        # rebuild from only the events of the first --max transactions (by ns)
        cutoff=txns[a.max-1]["ns"]; evs=[e for e in evs if e["ns"]<=cutoff]; w,txns=build(evs)
    emit_vcd(w,a.out+".vcd")
    json.dump(txns,open(a.out+".json","w"),indent=2)
    print(f"bus {a.bus}: {len(evs)} events -> {len(txns)} transactions -> {a.out}.vcd ({len(w.ev)} edges)")
    from collections import Counter
    c=Counter((t['addr'],t['dir']) for t in txns)
    for (ad,dr),n in c.most_common(10):
        print(f"  0x{ad:02x} {dr}: {n}")

if __name__=="__main__": main()
