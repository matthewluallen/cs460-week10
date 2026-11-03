import os,shutil,subprocess,time
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"; PPM="/home/claude/fwtest/lcd.ppm"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
for p in (PPM,):
    try:os.remove(p)
    except OSError:pass
env=dict(os.environ); env["TBD_LCD_PPM"]=PPM
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-serial","null","-serial","null","-serial",f"file:/home/claude/fwtest/lcd_cdc.log","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=env)
time.sleep(16)
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
print("ppm exists:", os.path.exists(PPM), "size:", os.path.getsize(PPM) if os.path.exists(PPM) else 0)
