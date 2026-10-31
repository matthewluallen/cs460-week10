#!/usr/bin/env python3
"""assessment/verify-lab.py — per-part machine-checkable anchors for the Week-10/11 lab.

Drives whichever target is running and asserts the observable each learning objective names
(see assessment/objectives.md). The SAME assertions run on the physical StampLC and on the
QEMU digital twin — that is the parity the two-target environment exists to teach. Only the
transport differs:

  twin      (default)  : MQTT-free serial console over a Unix socket exposed by ./qemu/run.sh
                         (SERIAL_SOCK, default /tmp/plc-serial); HTTP on 127.0.0.1
  physical  StampLC    : USB serial port  (SERIAL_PORT=/dev/ttyACM0, needs pyserial);
                         HTTP at the board's IP  (OTA_HOST=<ip>:80  HMI_HOST=<ip>:8080)

Usage:
  ./qemu/run.sh                              # boot the twin (vulnerable) in one shell
  python assessment/verify-lab.py            # all applicable parts, in another shell
  python assessment/verify-lab.py P5         # one or more parts: P1 P3 P5 ...
  python assessment/verify-lab.py --boot-twin           # boot a twin if none is running, then verify
  ./qemu/run.sh secure ; python assessment/verify-lab.py P9     # Part 9 needs the secure build
  SERIAL_PORT=/dev/ttyACM0 OTA_HOST=10.0.0.5:80 HMI_HOST=10.0.0.5:8080 \
      python assessment/verify-lab.py        # physical board over USB + Ethernet

Verdicts:  PASS (observable achieved) · FAIL (not achieved) · SKIP (tool/target not available).
Exit 0 iff no requested check FAILed (a SKIP is not a failure). The printed verdict is the
"mechanics" share of the rubric; the student's written finding is the larger "reasoning" share.
"""
import argparse, os, re, select, shutil, signal, socket, subprocess, sys, tempfile, time
import urllib.request

ROOT      = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREBUILT  = os.path.join(ROOT, "sbom", "firmware", "prebuilt", "stamplc-factory-8MB.bin")
SERIAL    = os.environ.get("SERIAL_SOCK", "/tmp/plc-serial")
SERPORT   = os.environ.get("SERIAL_PORT")          # set => physical board
BAUD      = int(os.environ.get("BAUD", "115200"))
OTA_HOST  = os.environ.get("OTA_HOST", "127.0.0.1:80")
HMI_HOST  = os.environ.get("HMI_HOST", "127.0.0.1:8080")
NTP_HOST  = os.environ.get("NTP_HOST", "127.0.0.1")
NTP_PORT  = os.environ.get("NTP_PORT", "12345")

BRIDGE_CON = int(os.environ.get("BRIDGE_CONSOLE_PORT", "8081"))

def _pgrep(pattern):
    try:
        return subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True).stdout.split()
    except FileNotFoundError:
        return []

def bridge_holds_console():
    """True when ./qemu/studio.sh's bridge is attached to this twin and its console port answers."""
    if not _pgrep(f"twin-bridge.py.*--serial {SERIAL}"):
        return False
    try:
        socket.create_connection(("127.0.0.1", BRIDGE_CON), timeout=1).close()
        return True
    except OSError:
        return False

def twin_already_running():
    """Same rule as qemu/twin-status.sh: a live qemu process on THIS socket AND the socket answers."""
    if not _pgrep(f"qemu-system-xtensa.*unix:{SERIAL},server"):
        return False
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(1.5)
    try:
        s.connect(SERIAL); return True
    except OSError:
        return False
    finally:
        s.close()

# ------------------------------------------------------------------ transport
class Console:
    """A line console over either a Unix socket (twin) or a serial port (physical)."""
    def __init__(self):
        self.kind = "serial" if SERPORT else "socket"
        self._ser = None
        self._sock = None

    def open(self):
        if self.kind == "serial":
            try:
                import serial  # pyserial
            except ImportError:
                sys.exit("SERIAL_PORT set but pyserial is missing — pip install pyserial")
            self._ser = serial.Serial(SERPORT, BAUD, timeout=0)
        elif bridge_holds_console():
            # ./qemu/studio.sh is running: its bridge owns the twin's ONE console connection,
            # so a second direct client would sit unanswered. Attach through the bridge instead.
            self.kind = "bridge"
            s = socket.create_connection(("127.0.0.1", BRIDGE_CON), timeout=5)
            s.setblocking(False)
            self._sock = s
        else:
            t0 = time.time()
            while not os.path.exists(SERIAL):
                if time.time() - t0 > 30:
                    sys.exit(f"twin serial socket never appeared: {SERIAL} — start it: ./qemu/studio.sh")
                time.sleep(0.2)
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(SERIAL); s.setblocking(False)
            self._sock = s

    def _read(self):
        if self.kind == "serial":
            try: return self._ser.read(4096)
            except Exception: return b""
        r, _, _ = select.select([self._sock], [], [], 0.2)
        if r:
            try: return self._sock.recv(4096)
            except BlockingIOError: return b""
        return b""

    def drain(self, dur):
        out = b""; end = time.time() + dur
        while time.time() < end:
            d = self._read()
            if d: out += d
            elif self.kind == "serial": time.sleep(0.1)
        return out.decode("utf-8", "replace")

    def send(self, line):
        data = line.encode() + b"\n"
        if self.kind == "serial": self._ser.write(data)
        else: self._sock.send(data)

    def cmd(self, line, wait):
        self.send(line); return self.drain(wait)

    def label(self):
        if self.kind == "serial": return f"physical StampLC ({SERPORT})"
        if self.kind == "bridge": return f"twin ({SERIAL}, via the Studio bridge on :{BRIDGE_CON})"
        return f"twin ({SERIAL})"

    def close(self):
        try:
            if self._ser: self._ser.close()
            if self._sock: self._sock.close()
        except Exception: pass

def relay(txt, n):
    m = re.search(rf"relay{n}\s+cmd=\d+\s+act=(\d)", txt)
    return m.group(1) if m else "?"

def poll_relay(c, want, timeout=20):
    end = time.time() + timeout; last = ""
    while time.time() < end:
        last = c.cmd("io", 1.2)
        if all(relay(last, n) == v for n, v in want.items()):
            return True, last
    return False, last

def http(url, timeout=6):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, (e.read().decode("utf-8", "replace") if e.fp else "")
    except Exception as e:
        return None, f"{type(e).__name__}: {e}"

# ------------------------------------------------------------------ checks
# Each returns (verdict, evidence). device=True checks need a running target.
PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"

def P1_bus(_c):
    """Offline: the recorded request decodes to FC3, register 25."""
    dec = os.path.join(ROOT, "bus-analysis", "tools", "decode.js")
    req = os.path.join(ROOT, "bus-analysis", "serial", "physical-0-request.bin")
    if not (shutil.which("node") and os.path.exists(dec) and os.path.exists(req)):
        return SKIP, "node / decoder / sample not available"
    out = subprocess.run(["node", dec, "rtu-request", req],
                         capture_output=True, text=True).stdout
    ok = '"status": "valid"' in out and '"value": 3' in out and '"value": 25' in out
    return (PASS if ok else FAIL), "sample decodes to status=valid, FC03, register 25"

def P2_sbom(_c):
    """Offline: an SPDX and a CycloneDX SBOM both exist from the same tree and differ."""
    cand = []
    for base in ("sbom/out", "sbom"):
        d = os.path.join(ROOT, base)
        if os.path.isdir(d):
            cand += [os.path.join(d, f) for f in os.listdir(d)]
    spdx = [f for f in cand if "spdx" in f.lower()]
    cdx  = [f for f in cand if "cyclonedx" in f.lower() or "cdx" in f.lower()]
    if not (spdx and cdx):
        return SKIP, "no SPDX+CycloneDX outputs yet — run sbom/run_sbom.sh first"
    return PASS, f"both SBOM formats present ({os.path.basename(spdx[0])}, {os.path.basename(cdx[0])})"

def P3_hbom(c):
    """Device: i2cscan matches the HBOM — six internal devices + PORT.A 0x77."""
    scan = c.cmd("i2cscan", 2.5).lower().replace("0X", "0x")
    internal = [a for a in ("0x20", "0x32", "0x40", "0x43", "0x48", "0x59") if a in scan]
    w1 = "0x77" in scan
    ok = len(internal) == 6 and w1
    return (PASS if ok else FAIL), f"{len(internal)}/6 internal I2C + PORT.A 0x77={w1}"

def P4_ladder(_c):
    """Offline: the example projects compile to IR with the ST<->ladder pairing intact."""
    probe = subprocess.run(["node", "-e", "require('playwright')"], cwd=ROOT,
                           capture_output=True, text=True)
    if shutil.which("node") is None or probe.returncode != 0:
        return SKIP, "node/playwright not installed (run `npm i playwright` to enable)"
    r = subprocess.run(["node", os.path.join(ROOT, "tools", "studio-regression.js")],
                       cwd=ROOT, capture_output=True, text=True)
    return (PASS if r.returncode == 0 else FAIL), (r.stdout.strip().splitlines() or ["(no output)"])[-1]

def P5_wire(c):
    """Device: di0_start -> relay0 & relay1; e-stop drops motor, latches fault lamp."""
    c.cmd("mode run", 2.0)
    c.cmd("force di0_start 1", 0.5)
    ok1, io1 = poll_relay(c, {0: "1", 1: "1"}, timeout=25)
    c.cmd("force di2_estop_in 1", 0.5)
    ok2, io2 = poll_relay(c, {0: "0", 3: "1"}, timeout=25)
    c.cmd("unforce di0_start", 1.0); c.cmd("unforce di2_estop_in", 1.0)
    ok = ok1 and ok2
    return (PASS if ok else FAIL), (f"di0->relay0={relay(io1,0)},relay1={relay(io1,1)}; "
                                    f"estop->relay0={relay(io2,0)},relay3(fault)={relay(io2,3)}")

def P6_ota(c):
    """Device: the OTA endpoint is present and advertises an unauthenticated upload path."""
    s, b = http("http://" + OTA_HOST + "/")
    ok = s == 200 and "/api/upload" in b and "OTA" in b
    return (PASS if ok else FAIL), f"OTA :{OTA_HOST} status={s}, advertises /api/upload={'/api/upload' in (b or '')}"

def P7_supply(_c):
    """Twin: requires the published malicious image to detonate the dormant payload."""
    mal = os.path.join(ROOT, "qemu", "twin", "stamplc-flash-8MB-MALICIOUS.bin")
    if not os.path.exists(mal):
        return SKIP, "malicious image not fetched (./qemu/fetch-qemu.sh) — twin-only part"
    return SKIP, "detonation is an interactive attacker step; see supply-chain/README.md (manual)"

def P8_ntp(c):
    """Device: benign NTP sync shows no truncation; an armed rogue server wraps the clock to ~1902."""
    srv = os.path.join(ROOT, "ntp-aitm", "ntp_server.py")
    if not os.path.exists(srv):
        return SKIP, "ntp-aitm/ntp_server.py not present"
    state = tempfile.NamedTemporaryFile(delete=False, suffix="-ntp-state").name
    try: os.unlink(state)
    except FileNotFoundError: pass
    p = subprocess.Popen(["python3", srv],
                         env=dict(os.environ, NTP_PORT=NTP_PORT, NTP_STATE=state),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.5)
        c.cmd(f"ntpsync {NTP_HOST} {NTP_PORT}", 6)
        cb = c.cmd("clk", 1.5)
        benign = "no truncation" in cb and "UNSYNCED" not in cb
        with open(state, "w") as f: f.write("arm")
        c.cmd(f"ntpsync {NTP_HOST} {NTP_PORT}", 6)
        cw = c.cmd("clk", 1.5)
        wrapped = "WRAPPED" in cw
        ok = benign and wrapped
        return (PASS if ok else FAIL), f"benign_clean={benign}, armed_wrap_to_1902={wrapped}"
    finally:
        p.terminate()
        try: os.unlink(state)
        except FileNotFoundError: pass

def P9_secure(c):
    """Device (SECURE build): the overflow force is blocked and the clock no longer wraps.
    Uses only safe reads (no firmware POST). On a vulnerable target it SKIPs with guidance."""
    OVF = "00" * 32 + "0200000000000000" + "0200000000000000"
    c.cmd("unforce di0_start", 0.8)
    base0 = relay(c.cmd("io", 1.2), 0) == "0"
    http("http://" + HMI_HOST + "/api/tag?data=" + OVF)
    forced, _ = poll_relay(c, {0: "1"}, timeout=8)
    overflow_blocked = base0 and not forced
    # NTP wrap behavior on this build
    ntp_v, ntp_ev = P8_ntp(c)
    no_wrap = "armed_wrap_to_1902=False" in ntp_ev
    if forced and not no_wrap:
        return SKIP, "target behaves as the VULNERABLE build — boot ./qemu/run.sh secure for Part 9"
    ok = overflow_blocked and no_wrap
    return (PASS if ok else FAIL), f"overflow_blocked={overflow_blocked}, clock_no_wrap={no_wrap}"

def W12_formal(_c):
    """Offline (Week 12): Z3 proves all six security controls on the verified build (Level 3)."""
    script = os.path.join(ROOT, "formal-verify", "verify_controls.py")
    if not os.path.exists(script):
        return SKIP, "formal-verify/verify_controls.py not present"
    r = subprocess.run([sys.executable, script, "--profile", "verified", "--assert-proven"],
                       capture_output=True, text=True)
    blob = (r.stdout or "") + (r.stderr or "")
    if "No module named 'z3'" in blob or "ModuleNotFoundError" in blob:
        return SKIP, "z3-solver not installed (pip install z3-solver)"
    if r.returncode == 0:
        return PASS, "Z3 proved all six controls on the verified build"
    return FAIL, (r.stdout.strip().splitlines() or ["proof assertion failed"])[-1]

CHECKS = {
    "P1": (P1_bus,    False, "Bus analysis — Modbus decode"),
    "P2": (P2_sbom,   False, "SBOM — SPDX vs CycloneDX"),
    "P3": (P3_hbom,   True,  "HBOM — i2cscan reconciliation"),
    "P4": (P4_ladder, False, "Ladder — compiles to IR, pairing holds"),
    "P5": (P5_wire,   True,  "Wire & run — input->logic->output + interlock"),
    "P6": (P6_ota,    True,  "Unsigned OTA — endpoint accepts"),
    "P7": (P7_supply, False, "Supply-chain kill switch (twin, manual)"),
    "P8": (P8_ntp,    True,  "NTP AiTM / Y2038 wrap"),
    "P9": (P9_secure, True,  "Remediation — secure build refuses the attacks"),
    "W12":(W12_formal,False, "Formal verification — Z3 proves all controls (Level 3)"),
}
# Default set when none named: everything except P9 (which needs the secure target).
DEFAULT = ["P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"]

# ------------------------------------------------------------------ twin boot (for --boot-twin / CI)
def boot_twin():
    import platform
    pd = {("Linux", "x86_64"): "qemu-twin-linux-x86_64",
          ("Linux", "aarch64"): "qemu-twin-linux-arm64",
          ("Darwin", "arm64"): "qemu-twin-macos-arm64"}.get((platform.system(), platform.machine()))
    qemu = os.path.join(ROOT, "qemu", "twin", pd or "", "qemu-system-xtensa")
    if not pd or not os.path.isfile(qemu):
        sys.exit("twin not fetched for this platform — run ./qemu/fetch-qemu.sh first")
    data = open(PREBUILT, "rb").read()
    img = tempfile.NamedTemporaryFile(delete=False, suffix="-verify-8MB.bin").name
    with open(img, "wb") as f: f.write(data); f.write(b"\xff" * (8 * 1024 * 1024 - len(data)))
    for p in (SERIAL,):
        try: os.unlink(p)
        except FileNotFoundError: pass
    env = dict(os.environ, FLASH_IMG=img, SERIAL_SOCK=SERIAL,
               QMP_SOCK="/tmp/plc-verify-qmp", ICOUNT="shift=3,align=off,sleep=off")
    proc = subprocess.Popen(["bash", os.path.join(ROOT, "qemu", "run.sh")], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    return proc, img

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("parts", nargs="*", help="e.g. P1 P3 P5 (default: P1–P8)")
    ap.add_argument("--boot-twin", action="store_true",
                    help="boot the QEMU twin if none is already running, verify, and tear down only a twin it booted")
    args = ap.parse_args()
    want = [p.upper() for p in args.parts] or list(DEFAULT)
    bad = [p for p in want if p not in CHECKS]
    if bad: sys.exit(f"unknown part(s): {bad} — choose from {list(CHECKS)}")

    proc = img = None
    if args.boot_twin:
        if SERPORT:
            print("  note: SERIAL_PORT is set (physical board) — --boot-twin ignored", flush=True)
        elif twin_already_running():
            print(f"  twin already running on {SERIAL} — reusing it (not booting a second one)", flush=True)
        else:
            proc, img = boot_twin()

    c = Console()
    needs_device = any(CHECKS[p][1] for p in want)
    try:
        if needs_device:                       # offline-only runs need no target
            c.open()
            boot = ""; t0 = time.time()
            while time.time() - t0 < 40 and "plc>" not in boot:
                boot += c.drain(0.5)
            if "plc>" not in boot:
                print("  note: did not see a plc> prompt yet; device checks may fail", flush=True)
        print(f"== CS460 lab verifier ==  target: {c.label()}\n", flush=True)
        results = {}
        for p in want:
            fn, _dev, title = CHECKS[p]
            try:
                verdict, ev = fn(c)
            except Exception as e:
                verdict, ev = FAIL, f"{type(e).__name__}: {e}"
            results[p] = verdict
            print(f"  [{verdict}] {p}  {title}\n          {ev}", flush=True)
    finally:
        c.close()
        if proc is not None:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM); proc.wait(timeout=10)
            except Exception:
                try: os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                except Exception: pass
            if img:
                try: os.unlink(img)
                except Exception: pass

    npass = sum(v == PASS for v in results.values())
    nfail = sum(v == FAIL for v in results.values())
    nskip = sum(v == SKIP for v in results.values())
    print(f"\n== {npass} PASS · {nfail} FAIL · {nskip} SKIP ==", flush=True)
    return 1 if nfail else 0

if __name__ == "__main__":
    sys.exit(main())
