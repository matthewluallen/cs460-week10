#!/usr/bin/env python3
# Regression: TCP (Modbus/502) and UDP (BACnet/47808) share the W5500 in one
# running firmware. Proves the UDP branch did not disturb the TCP bridge.
import os, shutil, socket, subprocess, time, threading, struct

# Env-overridable for Codespaces/CI (QEMU_BIN, TWIN_IMG); TWIN_WORK = scratch.
Q=os.environ.get("QEMU_BIN","/home/claude/qemu-esp/build/qemu-system-xtensa")
M=os.environ.get("TWIN_IMG","/home/claude/fwtest/new-tbd.bin")
WORK=os.environ.get("TWIN_WORK",os.path.dirname(os.path.abspath(__file__)));os.makedirs(WORK,exist_ok=True)
F=os.path.join(WORK,"flash_coex.bin")
CON=os.path.join(WORK,"coex_con.sock")
W5LOG=os.path.join(WORK,"coex_w5.log")
WHOIS=bytes([0x81,0x0A,0x00,0x08,0x01,0x00,0x10,0x08])

shutil.copyfile(M,F)
for p in (CON,):
    try:os.remove(p)
    except OSError:pass
env=dict(os.environ,TBD_W5DBG="1")
qp=subprocess.Popen([Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
    "-object","can-bus,id=canbus0","-serial","null","-serial","null",
    "-serial",f"unix:{CON},server=on,wait=off","-nographic","-no-reboot"],
    stdout=subprocess.DEVNULL,stderr=open(W5LOG,"wb"),env=env)

def uconn(path):
    for _ in range(200):
        try:s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect(path);return s
        except OSError:time.sleep(0.1)
    return None
buf=[];stop=[False]
c=uconn(CON)
def reader():
    c.settimeout(0.3)
    while not stop[0]:
        try:
            d=c.recv(4096)
            if d:buf.append(d)
        except socket.timeout:pass
        except OSError:break
threading.Thread(target=reader,daemon=True).start()
t0=time.time()
while time.time()-t0<35 and b"live PLC debug" not in b"".join(buf): time.sleep(0.3)
time.sleep(0.8)
c.sendall(b"proto bacnet on\n"); time.sleep(3.0)   # let both TCP :502 and UDP :47808 be up

# --- TCP: Modbus/502 read holding regs ---
print("=== TCP path: Modbus/502 ===")
tcp_ok=False
sk=None
for _ in range(10):
    try:sk=socket.create_connection(("127.0.0.1",502),timeout=3);break
    except Exception:time.sleep(1.0)
if sk:
    pdu=struct.pack(">BHH",3,0,6)
    req=struct.pack(">HHHB",1,0,len(pdu)+1,1)+pdu
    try:
        sk.sendall(req);data=sk.recv(256)
        if len(data)>=9 and not(data[7]&0x80):
            bc=data[8];vals=data[9:9+bc]
            regs=[(vals[i]<<8)|vals[i+1] for i in range(0,len(vals)-1,2)]
            print(f"  [OK] read holding regs 0-5: fc={data[7]:#x} regs={regs}");tcp_ok=True
        else: print(f"  [?] reply {data.hex()}")
    except Exception as e: print(f"  [FAIL] {e}")
    sk.close()
else:
    print("  [FAIL] could not connect to :502")

# --- UDP: BACnet/47808 Who-Is -> I-Am ---
print("=== UDP path: BACnet/47808 ===")
udp_ok=False
us=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);us.settimeout(2.0)
for _ in range(4):
    try:
        us.sendto(WHOIS,("127.0.0.1",47808));data,peer=us.recvfrom(256)
        if len(data)>=21 and data[0]==0x81 and data[6]==0x10 and data[7]==0x00:
            inst=((data[9]<<24)|(data[10]<<16)|(data[11]<<8)|data[12])&0x3FFFFF
            print(f"  [OK] I-Am {len(data)}B, device instance={inst}");udp_ok=True;break
        else: print(f"  [?] {data.hex()}")
    except socket.timeout: print("  [.] timeout, retry")
    time.sleep(0.6)
us.close()

print(f"\n[RESULT] TCP={'OK' if tcp_ok else 'FAIL'}  UDP={'OK' if udp_ok else 'FAIL'}  "
      f"=> {'COEXIST OK' if (tcp_ok and udp_ok) else 'REGRESSION'}")

stop[0]=True;time.sleep(0.3);qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
try:
    wl=open(W5LOG,"rb").read().decode("utf-8","replace")
    print("\n=== bridge: listen/ESTAB (TCP) + UDP lines ===")
    for l in wl.splitlines():
        if any(k in l for k in ("listen","ESTABLISH","UDP")): print("   ",l)
except OSError:pass
