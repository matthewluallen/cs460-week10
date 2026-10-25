#!/usr/bin/env python3
"""
End-to-end validation of the CONTAINERIZED attacker (Part 8, NTP AiTM) in a
Codespace / dev container with docker-in-docker.

Unlike qemu/smoke-test.py (which runs the rogue NTP server on 127.0.0.1), this
proves the realistic topology: the rogue time server runs in a SEPARATE container
at a DISTINCT IP (172.30.0.10 on the `labnet` bridge), and the StamPLC twin reaches
it as its own host on the network — exactly what a lab attacker host looks like.

What it asserts:
  A. route     the dev-container host can reach the attacker's SNTP at
               172.30.0.10:1123 (dind bridge is routable from the host netns)
  B. boot      the twin reaches the `plc>` console
  C. benign    `ntpsync 172.30.0.10 1123` -> clk shows a current, un-truncated date
  D. detonate  arm the container, re-sync -> clk WRAPS past 2038 to ~1901

Prereqs (the wrapper attacker/codespace-e2e.sh does these first):
  - the twin fetched           ./qemu/fetch-qemu.sh
  - the attacker container up   docker compose -f attacker/docker-compose.yml up -d --build

Exit 0 = all four legs passed.
"""
import os, sys, socket, select, time, subprocess, signal, tempfile, platform, struct, re

ROOT     = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREBUILT = os.path.join(ROOT, "sbom", "firmware", "prebuilt", "stamplc-factory-8MB.bin")
SERIAL   = os.environ.get("E2E_SERIAL", "/tmp/plc-e2e-serial")
QMP      = os.environ.get("E2E_QMP", "/tmp/plc-e2e-qmp")
ATTACKER = os.environ.get("ATTACKER_IP", "172.30.0.10")
NTP_PORT = int(os.environ.get("ATTACKER_NTP_PORT", "1123"))
NTP_DELTA = 2208988800   # 1900->1970

results = []
def check(name, ok, detail=""):
    results.append(ok)
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail else ""), flush=True)
    return ok

def platform_dir():
    d = {("Linux","x86_64"):"qemu-twin-linux-x86_64",
         ("Linux","aarch64"):"qemu-twin-linux-arm64",
         ("Darwin","arm64"):"qemu-twin-macos-arm64"}.get((platform.system(), platform.machine()))
    if not d:
        sys.exit(f"unsupported platform {platform.system()}/{platform.machine()}")
    return d

def make_image():
    if os.environ.get("FLASH_IMG"):
        return os.environ["FLASH_IMG"]
    if not os.path.isfile(PREBUILT):
        sys.exit(f"prebuilt not found: {PREBUILT}")
    data = open(PREBUILT, "rb").read()
    img = tempfile.NamedTemporaryFile(delete=False, suffix="-e2e-8MB.bin").name
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

def cmd(sock, line, wait=2.0):
    sock.send(line.encode() + b"\n")
    return drain(sock, wait)

# ---- A. prove the host routes to the container's SNTP (one real SNTP round-trip) ----
def probe_sntp(ip, port, timeout=30):
    """Send a 48-byte SNTP request to the container and read the reply. Returns the
    decoded Unix seconds the server served, or None. Proves dind-bridge routing."""
    deadline = time.time() + timeout
    pkt = bytearray(48); pkt[0] = 0x23   # LI=0 VN=4 Mode=3 (client)
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.settimeout(2.0)
            s.sendto(bytes(pkt), (ip, port))
            data, _ = s.recvfrom(96)
            s.close()
            if len(data) >= 44:
                secs1900 = struct.unpack("!I", data[40:44])[0]
                return secs1900 - NTP_DELTA     # may be negative once armed (that's the point)
        except (socket.timeout, OSError):
            time.sleep(1.0)
    return None

def main():
    bdir = os.path.join(ROOT, "qemu", "twin", platform_dir())
    qemu = os.path.join(bdir, "qemu-system-xtensa")
    if not os.path.isfile(qemu):
        sys.exit(f"twin not fetched ({qemu}) — run ./qemu/fetch-qemu.sh first")

    print(f"== Containerized-attacker E2E ==\n  attacker: {ATTACKER}:{NTP_PORT}\n  qemu    : {qemu}", flush=True)

    # A. routing: the dev-container host reaches the attacker container's SNTP.
    secs = probe_sntp(ATTACKER, NTP_PORT)
    yr = time.gmtime(secs).tm_year if secs and secs > 0 else None
    check("route.host->attacker_sntp", secs is not None,
          f"served unix={secs}" + (f" ({yr})" if yr else ""))
    if secs is None:
        print("  (cannot reach the attacker container — is it up on labnet? "
              "docker compose -f attacker/docker-compose.yml up -d --build)", flush=True)
        return finish()

    img = make_image()
    for p in (SERIAL, QMP):
        try: os.unlink(p)
        except FileNotFoundError: pass

    env = dict(os.environ, FLASH_IMG=img, SERIAL_SOCK=SERIAL, QMP_SOCK=QMP,
               ICOUNT="shift=3,align=off,sleep=off")
    proc = subprocess.Popen(["bash", os.path.join(ROOT, "qemu", "run.sh")],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    sock = None
    try:
        if not wait_socket(SERIAL, 30):
            check("boot", False, "serial socket never appeared"); return finish(proc)
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SERIAL); sock.setblocking(False)

        # B. boot
        boot = ""; t0 = time.time()
        while time.time() - t0 < 40 and "plc>" not in boot:
            boot += drain(sock, 0.5)
        if not check("boot", "plc>" in boot, "reached plc> console"):
            return finish(proc, sock)

        # C. benign: point the device at the CONTAINER, not localhost.
        cmd(sock, f"ntpsync {ATTACKER} {NTP_PORT}", 6)
        cb = cmd(sock, "clk", 1.5)
        check("benign.no_truncation", "no truncation" in cb and "UNSYNCED" not in cb,
              "clk shows a current, un-truncated date from the container")

        # D. detonate: arm the container (play the attacker), re-sync -> wrap past 2038.
        subprocess.run(["bash", os.path.join(ROOT, "attacker", "arm.sh")],
                       check=False, stdout=subprocess.DEVNULL)
        time.sleep(1.0)
        # confirm the container now serves the post-2038 time (host-side probe)
        armed = probe_sntp(ATTACKER, NTP_PORT, timeout=8)
        check("detonate.container_armed", armed is not None and (armed < 0 or armed > 2147483647 - 1),
              f"container now serves unix={armed}")
        cmd(sock, f"ntpsync {ATTACKER} {NTP_PORT}", 6)
        cw = cmd(sock, "clk", 1.5)
        check("detonate.y2038_wrap", "WRAPPED" in cw, "twin clk wraps to ~1901 after the container spoof")
    finally:
        # leave the container benign for the next run
        subprocess.run(["bash", os.path.join(ROOT, "attacker", "benign.sh")],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if sock: sock.close()
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM); proc.wait(timeout=10)
        except Exception:
            try: os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except Exception: pass
    return finish()

def finish(*_):
    ok = all(results) and len(results) >= 5
    print(f"\n== {'ALL PASS' if ok else 'FAILURES'} ==  {sum(results)}/{len(results)} checks", flush=True)
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
