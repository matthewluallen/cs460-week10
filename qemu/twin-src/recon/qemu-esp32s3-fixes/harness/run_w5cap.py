import os,shutil,subprocess,time
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"
CDC="/home/claude/fwtest/w5_cdc.log"      # firmware Serial console (HWCDC)
DBG="/home/claude/fwtest/w5_spi.log"      # W5DBG stderr (SPI2 traffic)
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
for p in (CDC,DBG):
    try:os.remove(p)
    except OSError:pass
env=dict(os.environ); env["TBD_W5DBG"]="1"
dbgf=open(DBG,"wb")
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M",
    "-drive",f"file={F},if=mtd,format=raw",
    "-serial","null","-serial","null","-serial",f"file:{CDC}",
    "-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=dbgf,env=env)
time.sleep(16)
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
dbgf.close()
cdc=open(CDC,"rb").read().decode("utf-8","replace")
dbg=open(DBG,"rb").read().decode("utf-8","replace")
# show any ETH console lines
eth=[l for l in cdc.splitlines() if "[ETH]" in l or "[Modbus]" in l or "hardware=" in l]
print("=== ETH/Modbus console lines ===")
print("\n".join(eth) if eth else "(none)")
lines=dbg.splitlines()
print(f"\n=== W5DBG SPI2 events: {len(lines)} total ===")
# count by cs
from collections import Counter
cs=Counter(l.split("cs=")[-1] for l in lines if "cs=" in l)
print("by cs:",dict(cs))
print("\n--- first 80 events ---")
print("\n".join(lines[:80]))
