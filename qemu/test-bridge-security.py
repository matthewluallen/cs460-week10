#!/usr/bin/env python3
"""qemu/test-bridge-security.py — the Studio server only obeys its own page.

Starts (or reuses) the twin + Studio server with ./qemu/studio.sh, then checks:
  * it listens on localhost only (not every interface);
  * Modbus WRITES by GET are refused (a cross-site <img src> carries no Origin);
  * a write from another site's page (foreign Origin) is refused, from the Studio's own page accepted;
  * reads carry no Access-Control-Allow-Origin, so other sites cannot read process values;
  * the console WebSocket refuses a foreign Origin and accepts the Studio's own;
  * /field sets an input bit AT THE CHIP (the expander's `di`), which `io` then shows without a
    force flag — and refuses a foreign Origin.
Leaves the twin and server running (stop: ./qemu/twin-status.sh stop). Exit 0 iff all pass.
"""
import base64, http.client, os, re, socket, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(os.environ.get("BRIDGE_PORT", "8080"))
HOST = "127.0.0.1"
SELF = "http://localhost:%d" % PORT          # what the Studio page's Origin looks like
EVIL = "http://evil.example"
results = []

def check(ok, what, detail=""):
    results.append(ok)
    print("  %s  %s%s" % ("PASS" if ok else "FAIL", what, ("   (" + detail + ")") if detail else ""), flush=True)

def req(method, path, origin=None, host="localhost:%d" % PORT):
    c = http.client.HTTPConnection(HOST, PORT, timeout=8)
    h = {"Host": host}
    if origin: h["Origin"] = origin
    c.request(method, path, headers=h)
    r = c.getresponse(); body = r.read().decode("utf-8", "replace")
    hdrs = {k.lower(): v for k, v in r.getheaders()}
    c.close()
    return r.status, hdrs, body

def ws_status(origin):
    s = socket.create_connection((HOST, PORT), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    lines = ["GET /ws HTTP/1.1", "Host: localhost:%d" % PORT, "Upgrade: websocket", "Connection: Upgrade",
             "Sec-WebSocket-Key: " + key, "Sec-WebSocket-Version: 13"]
    if origin: lines.append("Origin: " + origin)
    s.sendall(("\r\n".join(lines) + "\r\n\r\n").encode())
    first = s.recv(256).decode("latin1").split("\r\n", 1)[0]
    s.close()
    return first

def console(cmd, wait=2.5):
    s = socket.create_connection((HOST, 8081), timeout=5); s.settimeout(0.3)
    s.sendall((cmd + "\n").encode()); out = b""; end = time.time() + wait
    while time.time() < end:
        try: out += s.recv(4096)
        except socket.timeout: pass
    s.close()
    return out.decode("utf-8", "replace")

def main():
    print("== Studio server security", flush=True)
    log = open("/tmp/test-bridge-studio.log", "w")
    subprocess.Popen(["bash", os.path.join(ROOT, "qemu", "studio.sh")], stdout=log, stderr=log,
                     start_new_session=True)
    for _ in range(150):
        try:
            if req("GET", "/logic_studio.html")[0] == 200: break
        except OSError: pass
        time.sleep(0.2)
    else:
        print("Studio server never came up — see /tmp/test-bridge-studio.log"); return 1

    # the twin's own Modbus service comes up a few seconds after the console; wait for a good read
    for _ in range(60):
        try:
            if '"ok": true' in req("GET", "/modbus?fc=3&addr=0&count=1")[2]: break
        except OSError: pass
        time.sleep(0.5)

    ss = subprocess.run(["ss", "-ltnH", "sport = :%d" % PORT], capture_output=True, text=True).stdout
    check("127.0.0.1:%d" % PORT in ss and "0.0.0.0:%d" % PORT not in ss and "*:%d" % PORT not in ss,
          "listens on localhost only", " ".join(ss.split()[3:4]) or ss.strip())

    st, _, _ = req("GET", "/modbus?fc=5&addr=3&value=1")
    check(st == 405, "Modbus write by GET refused (a link or <img> can't flip a coil)", "HTTP %d" % st)
    st, _, _ = req("POST", "/modbus?fc=5&addr=3&value=1", origin=EVIL)
    check(st == 403, "Modbus write from another site's page refused", "HTTP %d" % st)
    st, _, body = req("POST", "/modbus?fc=5&addr=3&value=0", origin=SELF)
    check(st == 200 and '"ok": true' in body, "Modbus write from the Studio's own page accepted", "HTTP %d %s" % (st, body[:90]))
    st, hdrs, _ = req("GET", "/modbus?fc=3&addr=0&count=2")
    check(st == 200 and "access-control-allow-origin" not in hdrs,
          "reads carry no Access-Control-Allow-Origin (other sites can't read values)", "HTTP %d" % st)
    st, _, _ = req("POST", "/modbus?fc=5&addr=3&value=0", origin=None)
    check(st == 200, "a non-browser client (no Origin, e.g. curl) can still write", "HTTP %d" % st)

    first = ws_status(EVIL)
    check(" 403 " in first, "console WebSocket from another site refused", first)
    first = ws_status(SELF)
    check(" 101 " in first, "console WebSocket from the Studio's own page accepted", first)
    first = ws_status(None)
    check(" 101 " in first, "console WebSocket without Origin (non-browser) accepted", first)

    st, _, body = req("POST", "/field?bit=2&v=1", origin=EVIL)
    check(st == 403, "/field write from another site refused", "HTTP %d" % st)
    st, _, body = req("POST", "/field?bit=2&v=1", origin=SELF)
    check(st == 200 and '"ok": true' in body and '"di": 4' in body, "/field sets IN3 (di2) at the expander", body[:80])
    time.sleep(1.5)
    io = ""
    for _ in range(3):
        io = console("io")
        if re.search(r"DI\s*:", io): break
    m = re.search(r"DI\s*:\s*([0-9F ]+)", io)
    bits = m.group(1).split() if m else []
    check(bits[2:3] == ["1"] and "F" not in m.group(1),
          "`io` shows DI2 = 1 with no force flag (it came over the I2C bus)",
          ("DI : " + " ".join(bits)) if m else "no DI line: %r" % io[:120])
    st, _, body = req("POST", "/field?di=0", origin=SELF)
    check(st == 200 and '"di": 0' in body, "/field restores the input byte", body[:60])

    n_ok = sum(results)
    print("\n== %d passed, %d failed ==" % (n_ok, len(results) - n_ok))
    return 0 if all(results) else 1

if __name__ == "__main__":
    sys.exit(main())
