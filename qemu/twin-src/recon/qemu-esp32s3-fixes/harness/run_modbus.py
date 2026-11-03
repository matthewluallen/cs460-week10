import os,shutil,socket,subprocess,time,struct
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"; CDC="/home/claude/fwtest/mb_cdc.log"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
try:os.remove(CDC)
except OSError:pass
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-serial","null","-serial","null","-serial",f"file:{CDC}","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def net_ready(t):
    t0=time.time()
    while time.time()-t0<t:
        try:
            if "network up" in open(CDC).read() or "Modbus] TCP" in open(CDC).read(): return True
        except OSError: pass
        time.sleep(0.3)
    return False
print("[harness] network up:", net_ready(30))
time.sleep(2)
# one persistent connection, like a real Modbus master (mbpoll / SCADA)
sk=None
for attempt in range(10):
    try: sk=socket.create_connection(("127.0.0.1",502),timeout=3); break
    except Exception: time.sleep(1.0)
if not sk:
    print("[FAIL] could not connect to :502")
else:
    print("[OK] connected to 127.0.0.1:502 (persistent)")
    tid=0
    def modbus(fc,addr,qty,label,extra=b""):
        global tid; tid+=1
        pdu=struct.pack(">BHH",fc,addr,qty)+extra
        req=struct.pack(">HHHB",tid,0,len(pdu)+1,1)+pdu
        try:
            sk.sendall(req); data=sk.recv(256)
            if len(data)>=9 and not (data[7]&0x80):
                bc=data[8]; vals=data[9:9+bc]
                regs=[(vals[i]<<8)|vals[i+1] for i in range(0,len(vals)-1,2)]
                print(f"[OK] {label}: fc={data[7]:#x} bc={bc} data={regs if fc in(3,4) else vals.hex()}")
            else:
                print(f"[?] {label}: reply {data.hex()}")
        except Exception as e:
            print(f"[FAIL] {label}: {e}")
        time.sleep(0.5)
    modbus(3,0,6,"read holding regs 0-5")
    modbus(4,0,4,"read input regs 0-3")
    modbus(1,0,8,"read coils 0-7")
    modbus(2,0,8,"read discrete inputs 0-7")
    # consequence: write coil 0 = ON (unauth Modbus write, CWE-306), then read back
    modbus(5,0,0xFF00,"WRITE coil0=ON (unauth)",extra=b"")
    modbus(1,0,8,"re-read coils 0-7")
    sk.close()
time.sleep(1)
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
c=open(CDC,"rb").read().decode("utf-8","replace")
print("\n=== console: ETH/Modbus lines ===")
for l in c.splitlines():
    if any(k in l for k in ("[ETH]","[Modbus","Startup complete","[Net","listen")): print(l)
