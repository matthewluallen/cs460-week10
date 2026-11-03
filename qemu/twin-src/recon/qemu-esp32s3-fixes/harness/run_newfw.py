import os,shutil,socket,subprocess,time,struct,re
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/home/claude/fwtest/new-tbd.bin"
F="/home/claude/fwtest/flash_new.bin"; CDC="/home/claude/fwtest/nf_cdc.log"; U0="/home/claude/fwtest/nf_u0.log"
shutil.copyfile(M,F)  # already 8MB
for p in (CDC,U0):
    try:os.remove(p)
    except OSError:pass
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-object","can-bus,id=canbus0",
    "-serial",f"file:{U0}","-serial","null","-serial",f"file:{CDC}","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def rd():
    s=""
    for p in (U0,CDC):
        try:s+=open(p,errors="replace").read()
        except OSError:pass
    return s
t0=time.time(); booted=False
while time.time()-t0<40:
    if "mode=RUN" in rd() or "Startup complete" in rd(): booted=True; break
    time.sleep(0.4)
time.sleep(3)
txt=rd()
# Modbus/TCP :502 read to confirm network path + no regression
mb="(not attempted)"
try:
    sk=socket.create_connection(("127.0.0.1",502),timeout=4)
    pdu=struct.pack(">BHH",3,0,3)
    sk.sendall(struct.pack(">HHHB",1,0,len(pdu)+1,1)+pdu)
    d=sk.recv(256); bc=d[8]; regs=[(d[9+i*2]<<8)|d[9+i*2+1] for i in range(bc//2)]
    mb=f"OK fc={d[7]} regs0-2={regs}"; sk.close()
except Exception as e:
    mb=f"FAIL {e}"
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
print("booted:",booted,"| modbus/tcp:502:",mb)
kws=("Startup complete","mode=RUN","Modbus] TCP","BACnet","47808","Who-Is","I-Am",
     "socket","service","cap","budget","reaper","W5500","hardware=","HTTP","OPC","proto","PROTO","SVC","faults=")
print("\n=== boot console — new-feature & health lines ===")
seen=set()
for l in txt.splitlines():
    if any(k in l for k in kws):
        s=l.strip()
        if s not in seen:
            seen.add(s); print("  ",s[:160])
