import os,shutil,socket,subprocess,time,threading
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/home/claude/fwtest/new-tbd.bin"
F="/home/claude/fwtest/flash_feat.bin"
CON="/home/claude/fwtest/feat_con.sock"
def boot(reset):
    if reset: shutil.copyfile(M,F)
    try:os.remove(CON)
    except OSError:pass
    args=[Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
          "-object","can-bus,id=canbus0",
          "-serial","null","-serial","null","-serial",f"unix:{CON},server=on,wait=off",
          "-nographic","-no-reboot"]
    return subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def uconn(path):
    for _ in range(200):
        try:s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect(path);return s
        except OSError:time.sleep(0.1)
    return None
def reader(sock,buf,stop):
    sock.settimeout(0.3)
    while not stop[0]:
        try:
            d=sock.recv(4096)
            if d:buf.append(d)
        except socket.timeout:pass
        except OSError:break

print("=== PHASE 1: enable bacnet + probe the 4-socket cap ===")
qp=boot(reset=True); c=uconn(CON); buf=[]; stop=[False]
threading.Thread(target=reader,args=(c,buf,stop),daemon=True).start()
t0=time.time()
while time.time()-t0<35 and b"live PLC debug" not in b"".join(buf): time.sleep(0.3)
time.sleep(0.8)
def send(cmd,wait=1.2):
    mark=len(b"".join(buf))
    c.sendall((cmd+"\n").encode()); time.sleep(wait)
    return b"".join(buf)[mark:].decode("utf-8","replace")
for cmd in ["proto bacnet on","proto dnp3 on","proto opcua on","proto ethernet-ip on","proto s7comm on"]:
    out=send(cmd)
    line=[l for l in out.splitlines() if ("proto" in l.lower() or "budget" in l.lower() or "max" in l.lower() or "refus" in l.lower() or "cap" in l.lower() or "->" in l)]
    print(f"  $ {cmd:24} -> {' | '.join(l.strip() for l in line[-2:]) if line else out.strip()[:90]}")
stop[0]=True; time.sleep(0.3); qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
time.sleep(1)

print("\n=== PHASE 2: reboot with persisted config; observe what starts ===")
qp=boot(reset=False); c=uconn(CON); buf=[]; stop=[False]
threading.Thread(target=reader,args=(c,buf,stop),daemon=True).start()
t0=time.time()
while time.time()-t0<35 and b"mode=RUN" not in b"".join(buf): time.sleep(0.3)
time.sleep(2)
txt=b"".join(buf).decode("utf-8","replace")
kws=("BACnet","47808","sockets:","Modbus] TCP","HTTP","DNP3","OPC","EtherNet","S7","enabled","budget","service","mode=RUN","faults=","hardware=")
print("  boot lines:")
seen=set()
for l in txt.splitlines():
    if any(k in l for k in kws):
        s=l.strip()
        if s not in seen: seen.add(s); print("   ",s[:150])
stop[0]=True; time.sleep(0.3); qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
