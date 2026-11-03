import os,shutil,subprocess,time,json,socket
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"; PPM="/home/claude/fwtest/lcd_io.ppm"; QS="/home/claude/fwtest/btn.sock"
shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
for p in (PPM,QS):
    try:os.remove(p)
    except OSError:pass
env=dict(os.environ); env["TBD_LCD_PPM"]=PPM
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-serial","null","-serial","null","-serial","file:/home/claude/fwtest/btn_cdc.log",
    "-qmp",f"unix:{QS},server=on,wait=off","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=env)
def conn():
    for _ in range(120):
        try:s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect(QS);return s
        except OSError:time.sleep(0.1)
time.sleep(12)
s=conn(); f=s.makefile("rwb",buffering=0)
def rc():
    while True:
        o=json.loads(f.readline())
        if "event" not in o: return o
rc(); f.write(b'{"execute":"qmp_capabilities"}\n'); rc()
def qom_list(path):
    f.write(json.dumps({"execute":"qom-list","arguments":{"path":path}}).encode()+b"\n")
    return rc().get("return",[])
# find pi4ioe canonical path by walking from /machine
def find(path,depth=0):
    if depth>6: return None
    for e in qom_list(path):
        nm=e.get("name"); typ=e.get("type","")
        child=path.rstrip('/')+'/'+nm
        if nm=="pi4ioe": return child
        if typ.startswith("child<") or e.get("type","").startswith("child"):
            r=find(child,depth+1)
            if r: return r
    return None
p=find("/machine")
print("pi4ioe path:", p)
def qom_set(path,prop,val):
    f.write(json.dumps({"execute":"qom-set","arguments":{"path":path,"property":prop,"value":val}}).encode()+b"\n")
    return rc()
if p:
    # press BtnA (bit2, active-low) once: navigate MODE->I/O ; pulse press/release
    for _ in range(1):
        print("press BtnA:", qom_set(p,"input",0xFB))   # 0xFF & ~0b100
        time.sleep(0.25)
        print("release   :", qom_set(p,"input",0xFF))
        time.sleep(0.6)
time.sleep(2)
qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
print("ppm:", os.path.exists(PPM))
