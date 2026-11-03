#!/usr/bin/env python3
# run_bacnet_udp.py - end-to-end BACnet/IP Who-Is -> I-Am over the W5500 UDP
# datapath. Boots the merged new firmware, enables the BACnet responder over the
# USB console, then sends a Who-Is to 127.0.0.1:47808 and validates the I-Am.
import os, shutil, socket, subprocess, time, threading

# Paths are env-overridable so this runs unchanged in Codespaces/CI (QEMU_BIN,
# TWIN_IMG) or locally (defaults). TWIN_WORK holds the ephemeral flash/socket/log.
Q   = os.environ.get("QEMU_BIN", "/home/claude/qemu-esp/build/qemu-system-xtensa")
M   = os.environ.get("TWIN_IMG", "/home/claude/fwtest/new-tbd.bin")
WORK = os.environ.get("TWIN_WORK", os.path.dirname(os.path.abspath(__file__)))
os.makedirs(WORK, exist_ok=True)
F   = os.path.join(WORK, "flash_udp.bin")
CON = os.path.join(WORK, "bac_con.sock")
W5LOG = os.path.join(WORK, "bac_w5.log")

BACNET_PORT = 47808
WHOIS = bytes([0x81,0x0A,0x00,0x08, 0x01,0x00, 0x10,0x08])  # BVLC uni + NPDU v1 + Who-Is

def boot(reset):
    if reset: shutil.copyfile(M, F)
    for p in (CON,):
        try: os.remove(p)
        except OSError: pass
    args = [Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
            "-object","can-bus,id=canbus0",
            "-serial","null","-serial","null","-serial",f"unix:{CON},server=on,wait=off",
            "-nographic","-no-reboot"]
    env = dict(os.environ, TBD_W5DBG="1")
    log = open(W5LOG,"wb")
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=log, env=env)

def uconn(path):
    for _ in range(200):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(path); return s
        except OSError: time.sleep(0.1)
    return None

def reader(sock, buf, stop):
    sock.settimeout(0.3)
    while not stop[0]:
        try:
            d = sock.recv(4096)
            if d: buf.append(d)
        except socket.timeout: pass
        except OSError: break

def decode_iam(pkt):
    # BVLC(4) NPDU(2) then APDU: 10 00 C4 <objid4> 22 <maxapdu2> 91 <seg> 22 <vendor2>
    if len(pkt) < 21 or pkt[0] != 0x81: return None
    a = pkt[6:]
    if a[0] != 0x10 or a[1] != 0x00: return None      # Unconfirmed-Request / I-Am
    if a[2] != 0xC4: return None                       # ObjectIdentifier tag
    objid = (a[3]<<24)|(a[4]<<16)|(a[5]<<8)|a[6]
    otype, inst = objid >> 22, objid & 0x3FFFFF
    maxapdu = (a[8]<<8)|a[9]
    seg     = a[11]
    vendor  = (a[13]<<8)|a[14]
    return dict(otype=otype, inst=inst, maxapdu=maxapdu, seg=seg, vendor=vendor)

print("=== boot fresh + enable BACnet responder over console ===")
qp = boot(reset=True); c = uconn(CON); buf=[]; stop=[False]
threading.Thread(target=reader, args=(c,buf,stop), daemon=True).start()
t0 = time.time()
while time.time()-t0 < 35 and b"live PLC debug" not in b"".join(buf): time.sleep(0.3)
time.sleep(0.8)

def send_console(cmd, wait=1.2):
    mark = len(b"".join(buf))
    c.sendall((cmd+"\n").encode()); time.sleep(wait)
    return b"".join(buf)[mark:].decode("utf-8","replace")

out = send_console("proto bacnet on")
line = [l.strip() for l in out.splitlines() if ("bacnet" in l.lower() or "budget" in l.lower() or "refus" in l.lower())]
print("  $ proto bacnet on ->", " | ".join(line[-2:]) if line else out.strip()[:120])

# give handle()'s retry-bind time to grab UDP 47808
time.sleep(2.5)

print("\n=== send Who-Is -> expect I-Am on UDP 47808 ===")
result = None
us = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
us.settimeout(2.0)
for attempt in range(6):
    try:
        us.sendto(WHOIS, ("127.0.0.1", BACNET_PORT))
        data, peer = us.recvfrom(256)
        print(f"  [attempt {attempt+1}] reply {len(data)}B from {peer}: {data.hex()}")
        result = decode_iam(data)
        if result: break
    except socket.timeout:
        print(f"  [attempt {attempt+1}] no reply (timeout)")
    time.sleep(0.8)
us.close()

print()
if result:
    print("  [PASS] I-Am decoded:")
    print(f"    object type   = {result['otype']}  (8 = device)")
    print(f"    device instance = {result['inst']}  (expect 1013710)")
    print(f"    max APDU      = {result['maxapdu']}  (expect 1476)")
    print(f"    segmentation  = {result['seg']}  (expect 3 = none)")
    print(f"    vendor id     = {result['vendor']}  (expect 1976)")
    ok = (result['otype']==8 and result['inst']==1013710 and
          result['maxapdu']==1476 and result['vendor']==1976)
    print("  [RESULT]", "BACnet Who-Is/I-Am round-trip OK" if ok else "fields MISMATCH")
else:
    print("  [FAIL] no valid I-Am received")

stop[0]=True; time.sleep(0.3); qp.terminate()
try: qp.wait(timeout=5)
except Exception: qp.kill()

print("\n=== W5500 bridge trace (UDP lines) ===")
try:
    wl = open(W5LOG,"rb").read().decode("utf-8","replace")
    udp = [l for l in wl.splitlines() if "UDP" in l]
    for l in udp[:20]: print("   ", l)
    if not udp: print("    (no UDP bridge lines - datapath not exercised)")
except OSError:
    print("    (no w5 log)")
