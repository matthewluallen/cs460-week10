import os,shutil,socket,subprocess,time,threading
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"
SOCK="/home/claude/fwtest/cdc.sock"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
for p in (SOCK,):
    try:os.remove(p)
    except OSError:pass
# hd2 (HWCDC console) on a unix socket we can both read and write
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M",
    "-drive",f"file={F},if=mtd,format=raw",
    "-serial","null","-serial","null",
    "-serial",f"unix:{SOCK},server=on,wait=off",
    "-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
# connect to the console socket
c=None
for _ in range(100):
    try:c=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);c.connect(SOCK);break
    except OSError:time.sleep(0.1);c=None
if not c: print("no socket"); qp.terminate(); raise SystemExit
buf=bytearray(); lock=threading.Lock(); stop=False
def reader():
    c.settimeout(0.5)
    while not stop:
        try:
            d=c.recv(4096)
            if d:
                with lock: buf.extend(d)
        except socket.timeout: pass
        except OSError: break
th=threading.Thread(target=reader,daemon=True); th.start()
def wait_for(tok,timeout):
    t0=time.time()
    while time.time()-t0<timeout:
        with lock:
            if tok.encode() in buf: return True
        time.sleep(0.2)
    return False
ok=wait_for("live PLC debug terminal",30)
print("[harness] startup reached console:",ok)
time.sleep(1.0)
# type into the live console to prove interactive RX works end-to-end
for cmd in ("help\n","svcinfo\n","status\n"):
    print(f"[harness] sending: {cmd!r}")
    c.sendall(cmd.encode()); time.sleep(1.5)
# let it run to prove no WDT reboot
time.sleep(10)
stop=True; time.sleep(0.6)
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
out=bytes(buf).decode("utf-8","replace")
# did it reboot? (second ESP-ROM banner)
reboots=out.count("ESP-ROM:esp32s3")
print(f"\n[harness] total bytes={len(out)}  ROM-banners(reboots+1)={reboots}")
print("="*70); print(out); print("="*70)
