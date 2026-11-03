import os,socket,subprocess,time
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
F="/home/claude/fwtest/flash.bin"          # existing 8MB image (not executed under qtest)
QT="/home/claude/fwtest/qtest.sock"
CANLOG="/home/claude/fwtest/canmon.log"
TWAI=0x6002B000
def reg(i): return TWAI + (i*4)            # SJA1000 register i -> MMIO word
for p in (QT,CANLOG):
    try:os.remove(p)
    except OSError:pass
env=dict(os.environ); env["TBD_CAN_LOG"]=CANLOG
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-object","can-bus,id=canbus0","-accel","qtest",
    "-qtest",f"unix:{QT},server=on,wait=off","-display","none","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=env)
# connect to the qtest socket
s=None
for _ in range(100):
    try:s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect(QT);break
    except OSError:time.sleep(0.1);s=None
assert s,"no qtest socket"
f=s.makefile("rwb",buffering=0)
def cmd(c):
    f.write((c+"\n").encode())
    while True:
        line=f.readline().decode().strip()
        if line.startswith("OK") or line.startswith("FAIL"): return line
def wb(addr,val): return cmd(f"writeb 0x{addr:x} 0x{val:x}")
def rb(addr):
    r=cmd(f"readb 0x{addr:x}"); return int(r.split()[1],16) if r.startswith("OK") else None

print("=== SJA1000/TWAI register-level bring-up (no firmware; driven via qtest) ===")
sr0=rb(reg(2))
print(f"  SR after reset        = 0x{sr0:02x}   (expect 0x3c: bus-off/reset defaults)")
# 1) PeliCAN mode (CDR bit7; handler forces it), stay in reset mode to configure
wb(reg(31),0x80)                     # CDR -> PeliCAN
mod0=rb(reg(0))
print(f"  MODE (in reset)       = 0x{mod0:02x}   (bit0=1 reset mode)")
# 2) acceptance filter accept-all (reset mode: regs16-23 = ACR0-3/AMR0-3), timing
for i in range(16,20): wb(reg(i),0x00)   # ACR0-3 = 0x00
for i in range(20,24): wb(reg(i),0xFF)   # AMR0-3 = 0xFF (accept all)
wb(reg(6),0x00); wb(reg(7),0x14); wb(reg(8),0x1a)  # BTR0/BTR1/OCR
# 3) enter operating (self-test) mode: MODE = STM(bit2), reset bit cleared
wb(reg(0),0x04)
mod1=rb(reg(0))
print(f"  MODE (operating,STM)  = 0x{mod1:02x}   (bit2=1 self-test, bit0=0 running)")
# 4) build a standard data frame id=0x123 dlc=2 data=AB CD in the TX buffer
ID=0x123; d=[0xAB,0xCD]
wb(reg(16),0x02)                      # frame info: std, data, DLC=2
wb(reg(17),(ID>>3)&0xFF)              # ID.10..3
wb(reg(18),(ID&0x7)<<5)               # ID.2..0 in bits7..5
wb(reg(19),d[0]); wb(reg(20),d[1])    # data bytes
# 5) transmit
wb(reg(1),0x01)                       # CMR: transmission request
time.sleep(0.3)
sr1=rb(reg(2))
print(f"  SR after TX request   = 0x{sr1:02x}   (bit2 TBS + bit3 TCS => transmit complete)")
f.close(); s.close(); qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
print("\n=== CAN bus monitor capture (frame that left the controller onto the bus) ===")
try:
    cap=open(CANLOG).read().strip()
    print("  "+cap.replace("\n","\n  ") if cap else "  (monitor captured nothing)")
except OSError:
    print("  (no monitor log)")
