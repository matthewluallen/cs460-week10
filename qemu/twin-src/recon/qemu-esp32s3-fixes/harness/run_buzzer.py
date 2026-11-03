import os,shutil,subprocess,time
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"; CDC="/home/claude/fwtest/bz_cdc.log"; BUZ="/home/claude/fwtest/buzzer.log"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
for p in (CDC,BUZ):
    try:os.remove(p)
    except OSError:pass
env=dict(os.environ); env["TBD_BUZZER_LOG"]=BUZ
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-serial","null","-serial","null","-serial",f"file:{CDC}","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=env)
t0=time.time()
while time.time()-t0<32:
    try:
        if "live PLC debug" in open(CDC,errors="replace").read(): break
    except OSError:pass
    time.sleep(0.4)
time.sleep(2)
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
print("=== buzzer tone events captured (firmware plays tone(1500,90) at boot) ===")
try:
    b=open(BUZ).read().strip()
    print(b if b else "(none captured)")
except OSError: print("(no buzzer log)")
