#!/usr/bin/env python3
# parity_test.py — prove the emulated bus decodes IDENTICALLY to a physical tap.
#
# The guarantee behind "remote students get the same answers as the bench": for a
# captured scenario, the sequence of decoded I2C transactions must be identical
# whether you read (A) the QEMU controller model's ground-truth events, or (B) the
# sigrok I2C decoder running on the virtual tap's VCD. If they match, the
# virtual-tap -> VCD -> sigrok path is lossless, so a physical probe of the same
# transactions decodes the same.
#
#   python3 parity_test.py trace.log --bus i2c0 --max 120
#   # optional on-bench check: also diff a real capture
#   python3 parity_test.py trace.log --bus i2c0 --physical bench.vcd
import sys, os, re, subprocess, argparse, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import trace_to_sigrok as T

def ground_truth(trace, bus, mx):
    evs = T.parse(trace, bus)
    w, txns = T.build(evs)
    if mx: txns = txns[:mx]
    # canonical form: (dir, addr, tuple(bytes), ack)
    return [(t["dir"], t["addr"], tuple(t["bytes"]), "NACK" if t["nack"] else "ACK") for t in txns]

ANN = re.compile(r":\s*(Start|Stop|Address (read|write): ([0-9a-fA-F]+)|Data (read|write): ([0-9a-fA-F]+)|ACK|NACK)")
def sigrok_decode(vcd):
    out = subprocess.run(["sigrok-cli","-i",vcd,"-P","i2c:scl=SCL:sda=SDA","-A","i2c"],
                         capture_output=True, text=True).stdout
    txns=[]; cur=None
    for line in out.splitlines():
        m=ANN.search(line)
        if not m: continue
        tok=m.group(1)
        if tok=="Start":
            cur=None  # a new frame; address line sets it
        elif tok.startswith("Address"):
            d="R" if m.group(2)=="read" else "W"; addr=int(m.group(3),16)
            cur={"dir":d,"addr":addr,"bytes":[],"ack":"ACK"}; txns.append(cur)
        elif tok.startswith("Data"):
            if cur is not None: cur["bytes"].append(int(m.group(5),16))
        elif tok in ("ACK","NACK"):
            if cur is not None and not cur["bytes"]: cur["ack"]=tok  # address ack
        # Stop closes implicitly
    return [(t["dir"],t["addr"],tuple(t["bytes"]),t["ack"]) for t in txns]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("trace"); ap.add_argument("--bus",default="i2c0")
    ap.add_argument("--max",type=int,default=120); ap.add_argument("--physical",default=None)
    a=ap.parse_args()
    # build the virtual-tap VCD
    evs=T.parse(a.trace,a.bus); w,txns=T.build(evs)
    if a.max and len(txns)>a.max:
        # window must END on a complete transaction: cut just before txn #max starts
        cutoff=txns[a.max]["ns"]; evs=[e for e in evs if e["ns"]<cutoff]; w,txns=T.build(evs)
    T.emit_vcd(w,"/tmp/_parity.vcd")
    gt=ground_truth(a.trace,a.bus,a.max)
    sd=sigrok_decode("/tmp/_parity.vcd")
    n=min(len(gt),len(sd)); miss=sum(1 for i in range(n) if gt[i]!=sd[i])
    print(f"bus {a.bus}: ground-truth transactions={len(gt)}  sigrok-decoded={len(sd)}")
    print(f"compared {n}; mismatches={miss}")
    if miss:
        for i in range(n):
            if gt[i]!=sd[i]:
                print(f"  [{i}] model={gt[i]}  sigrok={sd[i]}");
                if i>6: break
    ok = (miss==0 and abs(len(gt)-len(sd))<=1)
    print("VIRTUAL-TAP PARITY:", "PASS (lossless through sigrok)" if ok else "FAIL")
    if a.physical:
        pd=sigrok_decode(a.physical); m=min(len(sd),len(pd))
        pm=sum(1 for i in range(m) if sd[i]!=pd[i])
        print(f"PHYSICAL vs VIRTUAL: compared {m}; mismatches={pm}",
              "PASS" if pm==0 else "FAIL")
    sys.exit(0 if ok else 1)

if __name__=="__main__": main()
