import os,shutil,subprocess,time
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"; CDC="/home/claude/fwtest/cb_cdc.log"; ERR="/home/claude/fwtest/cb_err.log"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
for p in (CDC,ERR):
    try:os.remove(p)
    except OSError:pass
# Real firmware image + an internal CAN bus the TWAI/SJA1000 port binds to.
args=[Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
      "-object","can-bus,id=canbus0",
      "-serial","null","-serial","null","-serial",f"file:{CDC}","-nographic","-no-reboot"]
qp=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=open(ERR,"wb"))
t0=time.time(); ok=False
while time.time()-t0<35:
    try:
        t=open(CDC,errors="replace").read()
        if "live PLC debug" in t or "Modbus] TCP" in t: ok=True; break
    except OSError:pass
    time.sleep(0.4)
time.sleep(1)
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
err=open(ERR,errors="replace").read()
cdc=open(CDC,errors="replace").read()
print("booted-with-canbus:", ok)
print("qemu stderr (errors/warnings):", (err.strip()[-400:] or "(none)"))
# show the PLC reached normal operation (port bind caused no disruption)
lines=[l for l in cdc.splitlines() if any(k in l for k in ("PLC","Modbus","TWAI","CAN","boot","READY","scan"))]
print("--- console (last 8 relevant) ---")
for l in lines[-8:]: print("  ",l)
