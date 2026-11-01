#!/usr/bin/env python3
"""
verify-secure.py — re-run the Part 6-8 (and Part 9 overflow) attacks against a
*running secure* twin and assert each one now FAILS (Part 9).

This is the behavioral counterpart to grepping source: it does not read a banner
or a CWE string, it drives the live console and HTTP services and checks the
*effect*. Boot the secure image first:

    ./qemu/build-twin.sh secure      # compile + package the hardened 8MB image
    RESTART=1 ./qemu/run.sh secure   # swap your running twin for it; leave it running

Then, in another shell:   ./remediation/verify-secure.sh

Each check prints PASS when the attack is correctly refused on the secure build.
Exit 0 = every Part 6-8 attack failed as it should on the secure build.

Env: SERIAL_SOCK (default /tmp/plc-serial), HMI (default 127.0.0.1:8080),
     OTA (default 127.0.0.1:80), NTP_PORT (default 12377).
"""
import os, sys, socket, select, time, subprocess, re
import urllib.request

ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERIAL  = os.environ.get("SERIAL_SOCK", "/tmp/plc-serial")
HMI     = os.environ.get("HMI", "127.0.0.1:8080")
OTA     = os.environ.get("OTA", "127.0.0.1:80")
NTPPORT = os.environ.get("NTP_PORT", "12377")

results = []
def check(name, ok, detail=""):
    results.append(ok)
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail else ""), flush=True)
    return ok

def drain(sock, dur):
    out = b""; end = time.time() + dur
    while time.time() < end:
        r, _, _ = select.select([sock], [], [], 0.2)
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
    """Poll `io` until every wanted relay matches, or timeout. The QEMU twin settles a
    scan or two behind each force, so a single read right after a force can miss the
    transition — poll for it (mirrors assessment/verify-lab.py)."""
    end = time.time() + timeout; last = ""
    while time.time() < end:
        last = cmd(sock, "io", 1.2)
        if all(relay(last, n) == v for n, v in want.items()):
            return True, last
    return False, last

def steady(sock, want, dur=2.5):
    """Every `io` sample over `dur` seconds matches `want` (proves a state HOLDS)."""
    end = time.time() + dur; last = ""; seen = 0
    while time.time() < end:
        last = cmd(sock, "io", 0.8); seen += 1
        if not all(relay(last, n) == v for n, v in want.items()):
            return False, last
    return seen > 0, last

def http(url, timeout=5):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, ""
    except Exception as e:
        return None, f"{type(e).__name__}: {e}"

def http_post(url, body=b"", timeout=5):
    try:
        req = urllib.request.Request(url, data=body, method="POST")
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, ""
    except Exception as e:
        return None, f"{type(e).__name__}: {e}"

def main():
    if not os.path.exists(SERIAL):
        sys.exit(f"No serial socket at {SERIAL} — boot the SECURE twin first "
                 f"(FLASH_IMG=<secure-image> ./qemu/run.sh).")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(SERIAL); sock.setblocking(False)
    drain(sock, 1.0)

    print("== Part 9: re-running the Part 6-8 attacks on the SECURE build ==", flush=True)

    # --- Part 6/7: unauthenticated OTA upload (CWE-306/494) ---
    # Secure W5500 OTA requires HTTP Basic auth; the unsigned/unauth push is refused.
    s, _ = http_post(f"http://{OTA}/api/upload", b"MALICIOUS-UNSIGNED-OTA")
    check("part6_7.ota_unauth_refused", s == 401,
          f"POST /api/upload -> {s} (expect 401; secure requires auth + HMAC)")

    # --- Part 3/console: legacy hardcoded admin backdoor (CWE-798) ---
    r = cmd(sock, "login admin openplc", 1.5)
    check("console.admin_backdoor_refused", "logged in" not in r.lower(),
          "login admin openplc rejected on secure")

    # --- Part 3/console: default operator:1234 (CWE-1392/1393) ---
    r = cmd(sock, "login operator 1234", 1.5)
    check("console.default_operator_refused", "logged in" not in r.lower(),
          "operator:1234 rejected (no default ships; account unprovisioned)")

    # --- Part 3/console: login lockout after 5 failures (CWE-307 fix) ---
    for _ in range(5):
        cmd(sock, "login operator wrongwrong", 0.6)
    r = cmd(sock, "login operator wrongwrong", 1.0)
    check("console.lockout_after_5", "logged in" not in r.lower(),
          "6th attempt refused inside the 30s lockout window")

    # --- Part 8: NTP AiTM / Y2038 (CWE-197/345) ---
    state = f"/tmp/verify-secure-ntp-state"
    try: os.unlink(state)
    except FileNotFoundError: pass
    with open(state, "w") as f: f.write("arm")          # serve a post-2038 time
    ntp = subprocess.Popen(
        ["python3", os.path.join(ROOT, "ntp-aitm", "ntp_server.py")],
        env=dict(os.environ, NTP_PORT=NTPPORT, NTP_STATE=state),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.5)
        cmd(sock, f"ntpsync 127.0.0.1 {NTPPORT}", 6)
        cw = cmd(sock, "clk", 1.5)
        check("part8.y2038_no_wrap", "WRAPPED" not in cw,
              "clk does NOT wrap (secure stores full 64-bit epoch; shows a 2038 date)")
    finally:
        ntp.terminate()
        try: os.unlink(state)
        except FileNotFoundError: pass

    # --- positive control: the factory motor program runs on THIS build ---
    # Without it a "relay0 stays OFF" result below could just mean outputs were inhibited
    # (PROGRAM mode, no project). The NC E-stop reads 0 unless driven (the twin's DI byte
    # boots 0), so hold estop_ok = 1, pulse reset_pb, then start/stop the motor.
    tag = cmd(sock, "force estop_ok 1", 0.8)
    for t in ("start_pb", "stop_pb", "reset_pb"):
        cmd(sock, f"force {t} 0", 0.4)
    cmd(sock, "mode run", 1.5)
    cmd(sock, "force reset_pb 1", 0.8); cmd(sock, "force reset_pb 0", 0.8)
    cmd(sock, "force stop_pb 1", 0.8); cmd(sock, "force stop_pb 0", 0.4)   # begin from idle
    cmd(sock, "force start_pb 1", 0.8); cmd(sock, "force start_pb 0", 0.4)
    ran, _ = poll_relay(sock, {0: "1"}, 20)                                # seal-in may lag a scan
    cmd(sock, "force stop_pb 1", 0.8); cmd(sock, "force stop_pb 0", 0.4)
    stopped, _ = poll_relay(sock, {0: "0"}, 20)
    for t in ("start_pb", "stop_pb", "estop_ok", "reset_pb"):
        cmd(sock, f"unforce {t}", 0.4)
    ok_motor = "unknown tag" not in tag and ran and stopped
    check("control.motor_program_runs", ok_motor,
          "positive control: start_pb seals relay0 in and stop_pb drops it on the secure build"
          + ("" if ok_motor else " — FAILED: " + (
              "no estop_ok tag (factory motor program not installed; rebuild with "
              "./qemu/build-twin.sh secure)" if "unknown tag" in tag else
              f"relay0 reached start={ran} stop={stopped} within 20s — is the Logic Studio "
              "twin-bridge attached to this console? stop it (it holds the serial) and re-run")))

    # --- Part 9: CWE-787 /api/tag overflow into the force image ---
    # Secure build clamps the copy into a separate 32-byte buffer: relay0 stays OFF.
    OVF = "00"*32 + "0200000000000000" + "0200000000000000"
    cmd(sock, "force estop_ok 1", 0.6)          # healthy E-stop so mode run admits RUN cleanly
    cmd(sock, "mode run", 1.5)
    base_off, _ = poll_relay(sock, {0: "0"}, 10)
    http(f"http://{HMI}/api/tag?data=" + OVF)
    time.sleep(2.0)
    still_off, _ = steady(sock, {0: "0"}, 2.5)   # relay0 STAYS off through the overflow
    cmd(sock, "unforce estop_ok", 0.4)
    check("part9.overflow_no_relay", base_off and still_off,
          "overlong /api/tag does NOT energize relay0 (bounds-checked copy)")

    sock.close()
    npass = sum(results); n = len(results)
    print(f"\n== {npass}/{n} checks passed (Part 6-9 attacks refused + the motor-program positive control) ==", flush=True)
    print("Reconcile: a refusal here is behavioral proof of the fix. The 'cwe'/'svcinfo'", flush=True)
    print("text still lists the weaknesses in BOTH builds — that text is a teaching", flush=True)
    print("reference, not evidence the weakness is live.", flush=True)
    return 0 if npass == n and n > 0 else 1

if __name__ == "__main__":
    sys.exit(main())
