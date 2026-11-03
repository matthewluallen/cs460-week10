import os,shutil,socket,subprocess,time,struct,threading
Q="/home/claude/qemu-esp/build/qemu-system-xtensa"
M="/mnt/user-data/uploads/m5stamplc_openplc/firmware/.pio/build/m5stamplc/merged-tbd.bin"
F="/home/claude/fwtest/flash.bin"
CON="/home/claude/fwtest/rs_con.sock"; BUS="/home/claude/fwtest/rs_485.sock"

def crc16(d):
    c=0xFFFF
    for b in d:
        c^=b
        for _ in range(8):
            c=(c>>1)^0xA001 if (c&1) else c>>1
    return bytes([c&0xFF,(c>>8)&0xFF])

def boot(reset_nvs, con_only=True):
    if reset_nvs:
        shutil.copyfile(M,F); open(F,"r+b").truncate(8*1024*1024)
    for p in (CON,BUS):
        try:os.remove(p)
        except OSError:pass
    args=[Q,"-M","esp32s3","-m","8M","-drive",f"file={F},if=mtd,format=raw",
          "-serial","null",
          "-serial",(f"unix:{BUS},server=on,wait=off" if not con_only else "null"),
          "-serial",f"unix:{CON},server=on,wait=off","-nographic","-no-reboot"]
    return subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)

def uconn(path):
    for _ in range(150):
        try:s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect(path);return s
        except OSError:time.sleep(0.1)
    return None

def reader(sock,buf,stopf):
    sock.settimeout(0.4)
    while not stopf[0]:
        try:
            d=sock.recv(4096)
            if d:buf.append(d)
        except socket.timeout:pass
        except OSError:break

# ---- Phase 1: enable the RS-485 Modbus RTU service, persist to NVS ----
print("=== phase 1: enable proto modbus-rtu ===")
qp=boot(reset_nvs=True, con_only=True)
c=uconn(CON); buf=[]; stop=[False]; threading.Thread(target=reader,args=(c,buf,stop),daemon=True).start()
t0=time.time()
while time.time()-t0<28 and b"live PLC debug" not in b"".join(buf): time.sleep(0.3)
time.sleep(0.5); c.sendall(b"proto modbus-rtu on\n"); time.sleep(2.0)
print("  ", b"".join(buf).decode("utf-8","replace").splitlines()[-1] if buf else "(no console)")
stop[0]=True; time.sleep(0.4); qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
time.sleep(1)

# ---- Phase 2: reboot with the service active; talk Modbus RTU over RS-485 ----
print("=== phase 2: boot with RTU active, drive RS-485 ===")
qp=boot(reset_nvs=False, con_only=False)
c=uconn(CON); buf=[]; stop=[False]; threading.Thread(target=reader,args=(c,buf,stop),daemon=True).start()
t0=time.time()
while time.time()-t0<28 and b"live PLC debug" not in b"".join(buf): time.sleep(0.3)
time.sleep(1.0)
con=b"".join(buf).decode("utf-8","replace")
rtu_line=[l for l in con.splitlines() if "RTU" in l or "RS-485" in l or "PROTO" in l]
print("  console:", rtu_line[-2:] if rtu_line else "(no RTU banner)")
# RS-485 bus: send Modbus RTU read-holding-regs (slave 1, FC03, addr0, qty3)
bus=uconn(BUS)
if bus:
    req=bytes([1,3,0,0,0,3]); req+=crc16(req)
    bus.settimeout(3)
    print("  RTU >>", req.hex())
    bus.sendall(req)
    try:
        resp=bus.recv(256)
        print("  RTU <<", resp.hex())
        if len(resp)>=5 and resp[0]==1 and resp[1]==3:
            bc=resp[2]; regs=[(resp[3+i*2]<<8)|resp[3+i*2+1] for i in range(bc//2)]
            print(f"  [OK] RS-485 Modbus RTU: slave=1 fc=3 regs(0-2)={regs}  (Vin={regs[0]}mV I={regs[1]}mA temp={regs[2]/10:.1f}C)")
        else:
            print("  [?] unexpected RTU reply")
    except Exception as e:
        print("  [FAIL] no RTU reply:", e)
else:
    print("  [FAIL] no RS-485 bus socket")
stop[0]=True; time.sleep(0.4); qp.terminate()
try:qp.wait(timeout=5)
except Exception:qp.kill()
