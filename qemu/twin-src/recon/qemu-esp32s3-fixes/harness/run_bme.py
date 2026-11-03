import os,shutil,subprocess,time,re
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"
U0="/home/claude/fwtest/bme_uart0.log"; CDC="/home/claude/fwtest/bme_cdc.log"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
for p in (U0,CDC):
    try:os.remove(p)
    except OSError:pass
# serial0 = UART0 boot log (file), serial1 = null, serial2 = HWCDC live console (file)
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-serial",f"file:{U0}","-serial","null","-serial",f"file:{CDC}","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
t0=time.time()
def readall():
    s=""
    for p in (U0,CDC):
        try:s+=open(p,encoding="utf-8",errors="replace").read()
        except OSError:pass
    return s
# wait for a GROVE line (or time out ~35s)
grove=None
while time.time()-t0<35:
    txt=readall()
    m=[l for l in txt.splitlines() if "GROVE" in l or "BME" in l or "env_ok" in l or "ENV" in l]
    if m and any("env_ok" in l or "hPa" in l or "rh" in l.lower() for l in m):
        grove=m; break
    time.sleep(0.5)
time.sleep(1.5)
txt=readall()
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
lines=[l for l in txt.splitlines() if "GROVE" in l or "BME" in l or "env" in l.lower() or "ENV" in l or "0x77" in l]
print("=== GROVE / BME680 / ENV lines ===")
for l in lines[-25:]: print("  ", l)
if not lines:
    print("  (none found) — last 30 console lines:")
    for l in txt.splitlines()[-30:]: print("   |", l)
