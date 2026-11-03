import os,socket,subprocess,time,threading
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
F="/home/claude/fwtest/flash_feat.bin"   # persisted: bacnet+dnp3 enabled
CON="/home/claude/fwtest/diag_con.sock"
try:os.remove(CON)
except OSError:pass
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-object","can-bus,id=canbus0","-serial","null","-serial","null",
    "-serial",f"unix:{CON},server=on,wait=off","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
c=None
for _ in range(200):
    try:c=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);c.connect(CON);break
    except OSError:time.sleep(0.1)
buf=[];stop=[False]
def rd():
    c.settimeout(0.3)
    while not stop[0]:
        try:
            d=c.recv(4096)
            if d:buf.append(d)
        except socket.timeout:pass
        except OSError:break
threading.Thread(target=rd,daemon=True).start()
t0=time.time()
while time.time()-t0<35 and b"mode=RUN" not in b"".join(buf): time.sleep(0.3)
time.sleep(1)
def send(cmd,w=1.5):
    m=len(b"".join(buf)); c.sendall((cmd+"\n").encode()); time.sleep(w)
    return b"".join(buf)[m:].decode("utf-8","replace")
for cmd in ("faults","diag"):
    print(f"=== $ {cmd} ==="); print(send(cmd)[:1400])
stop[0]=True; time.sleep(0.3); qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
