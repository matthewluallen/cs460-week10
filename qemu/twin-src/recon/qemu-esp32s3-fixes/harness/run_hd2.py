import os,shutil,socket,subprocess,time
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"
U0="/home/claude/fwtest/hd0_uart.log"    # ESP-IDF logs (UART0)
U2="/home/claude/fwtest/hd2_cdc.log"     # firmware Serial.* (HWCDC / USB-Serial-JTAG)
shutil.copyfile(M,F)
open(F,"r+b").truncate(8*1024*1024)
for p in (U0,U2):
    try:os.remove(p)
    except OSError:pass
# hd0=UART0->file, hd1=null, hd2=HWCDC->file  (this is the console we could never see)
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M",
    "-drive",f"file={F},if=mtd,format=raw",
    "-serial",f"file:{U0}",
    "-serial","null",
    "-serial",f"file:{U2}",
    "-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(18)
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
def dump(tag,path):
    print(f"\n========== {tag} ({path}) ==========")
    try:
        d=open(path,"rb").read()
        print(f"[{len(d)} bytes]")
        print(d.decode("utf-8","replace"))
    except OSError as e:
        print("ERR",e)
dump("UART0 / ESP-IDF logs",U0)
dump("HWCDC / firmware Serial console",U2)
