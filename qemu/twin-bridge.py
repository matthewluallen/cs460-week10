#!/usr/bin/env python3
"""twin-bridge — serve the Logic Studio AND bridge it to the QEMU twin, on one port.

The Studio is a static web page; the twin's console is a local socket, not a USB
device, so the browser can't reach it over Web Serial. This one server solves both
halves at once:

  * it serves the Studio (and the rest of web/public) over HTTP,
  * it upgrades a WebSocket on /ws and relays it to the twin's console, and
  * it exposes the same console as a raw TCP port (localhost) for ./qemu/console.sh.

Because the page and the socket share one origin, the Studio's "Connect twin" URL is
just same-origin /ws — correct in a Codespace (one forwarded port, no host:port
guessing) and on a laptop (localhost) alike. One student, one port, no teams, no
hardware, no Netlify.

    ┌──────────┐  http (page) + ws:///ws   ┌──────────────┐  one client    ┌──────┐
    │ browser  │ ◀───── one origin ──────▶ │  twin-bridge  │ ──────────────▶│ twin │
    └──────────┘       :8080               │   (the hub)   │ /tmp/plc-serial└──────┘
    ┌──────────┐  raw TCP :8081 (local)    │  fans one     │
    │ console  │ ◀───────────────────────▶ │  console out  │
    └──────────┘                           └──────────────┘

Why a hub?  A QEMU `-serial unix:...,server=on` socket accepts ONE client. If the
bridge opened a fresh connection per browser tab (as it used to), a second client —
the text console, another tab — would connect but get nothing and appear to hang.
The hub holds the single twin connection and multiplexes it: device output is
broadcast to every subscriber (Studio tabs + console), and input from any subscriber
is written to the twin. So the Studio and `./qemu/console.sh` work at the same time.

Dependency-free (Python 3 stdlib only), so it runs in a Codespace or a laptop with
no `pip install`.

    ./qemu/studio.sh                # starts the twin + this bridge; open :8080
    ./qemu/console.sh               # attaches the text console (coexists with the Studio)

Options (env or flags):
    --port 8080                WebSocket + HTTP port (default 8080)
    --console-port 8081        raw TCP console port, localhost only (default 8081; 0 disables)
    --serial /tmp/plc-serial   twin console: a unix socket path, or host:port for TCP
    --dir <path>               static web root (default: ../web/public next to this script)
    --bind 127.0.0.1           address the Studio port listens on (default 127.0.0.1; the Docker
                               stations set BRIDGE_BIND=0.0.0.0 so the host port mapping reaches it)
    --qmp /tmp/plc-qmp         the twin's QMP socket, used ONLY by the whitelisted /field endpoint

Security model (this server can move a process, so it is locked to its own page):
  * it listens on localhost unless told otherwise (Codespaces forwards localhost ports);
  * a browser request that carries an Origin header must come from this server's own page
    (Origin host == Host or X-Forwarded-Host, or listed in BRIDGE_ALLOWED_ORIGINS) — this covers
    the console WebSocket and every state-changing request, so another web page the student has
    open cannot drive the twin;
  * Modbus WRITES (FC5/FC6) and /field writes need POST: a cross-site <img src> GET carries no
    Origin, so GET can only ever read;
  * no Access-Control-Allow-Origin: other sites cannot read process values through it either;
  * /field exposes exactly two QMP properties of the twin's AW9523 expander (`di` read/write,
    `relays` read) — never raw QMP, which could reset, pause or reconfigure the emulator.
  Clients without an Origin header (curl, scripts, the console) are not browsers and are allowed.
"""
import argparse, base64, hashlib, mimetypes, os, posixpath, socket, struct, sys, threading, time
import urllib.parse
import json as _json_mod

ALLOWED_ORIGINS = {o.strip().rstrip("/").lower()
                   for o in os.environ.get("BRIDGE_ALLOWED_ORIGINS", "").split(",") if o.strip()}

def origin_ok(headers):
    """Same-origin check for browser requests (see the security model in the module docstring)."""
    o = headers.get("origin")
    if not o:
        return True                          # not a browser cross-site request (curl, scripts)
    o = o.strip().rstrip("/").lower()
    if o in ALLOWED_ORIGINS:
        return True
    try:
        netloc = urllib.parse.urlparse(o).netloc
    except ValueError:
        return False
    for h in (headers.get("host", ""), headers.get("x-forwarded-host", "")):
        for part in h.split(","):
            if part.strip().lower() == netloc:
                return True
    return False

def _deny(conn, code, why):
    body = _json_mod.dumps({"ok": False, "error": why}).encode()
    conn.sendall(b"HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
                 b"Content-Length: %d\r\nConnection: close\r\n\r\n"
                 % (code, b"Forbidden" if code == 403 else b"Method Not Allowed", len(body)) + body)

QMP_SOCK = os.environ.get("QMP_SOCK", "/tmp/plc-qmp")
_qmp_lock = threading.Lock()
_FIELD_PATH = "/machine/soc/aw9523"

def _qmp(cmds, timeout=3.0):
    """Run a list of (execute, arguments) on the twin's QMP socket; return the list of returns."""
    with _qmp_lock:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(timeout)
        s.connect(QMP_SOCK)
        f = s.makefile("rw")
        try:
            f.readline()                                           # greeting
            out = []
            for ex, args in [("qmp_capabilities", None)] + list(cmds):
                m = {"execute": ex}
                if args is not None: m["arguments"] = args
                f.write(_json_mod.dumps(m) + "\n"); f.flush()
                while True:
                    r = _json_mod.loads(f.readline())
                    if "return" in r or "error" in r:
                        break
                if "error" in r:
                    raise RuntimeError(r["error"].get("desc", "qmp error"))
                out.append(r["return"])
            return out[1:]
        finally:
            s.close()

WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
INDEX = "logic_studio.html"   # what "/" serves

# Studio-hub extras: files reachable from the one Studio port but kept in their real
# home (the READMEs reference them there), so there is a single source of truth.
EXTRA_FILES = {
    "/i2c-analyzer.html": "bus-analysis/i2c-analyzer.html",
    "/bus-analyzer.html": "bus-analysis/i2c-analyzer.html",
}


# ───────────────────────── the hub: one twin, many subscribers ─────────────────────────
class TwinHub:
    """Owns the single connection to the twin console and fans it out.

    Device→subscribers is a broadcast; subscriber→device is serialized. Subscribers
    register a callback that receives raw bytes. The hub keeps trying to (re)connect
    so the Studio can be opened before, during or after the twin boots.
    """
    def __init__(self, serial):
        self.serial = serial
        self.twin = None
        self.lock = threading.Lock()         # guards self.twin for writes
        self.subs = set()                    # set of callables: cb(bytes)
        self.subs_lock = threading.Lock()
        threading.Thread(target=self._pump, daemon=True).start()

    def _connect(self):
        s = self.serial
        if ":" in s and not os.path.exists(s):
            host, port = s.rsplit(":", 1)
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((host, int(port)))
        else:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(s)
        return sock

    def _pump(self):
        """Hold one twin connection; broadcast everything it emits; reconnect on loss."""
        while True:
            try:
                sock = self._connect()
            except OSError:
                time.sleep(0.5)
                continue
            with self.lock:
                self.twin = sock
            print("[bridge] twin connected (%s)" % self.serial, flush=True)
            try:
                while True:
                    data = sock.recv(4096)
                    if not data:
                        break
                    self._broadcast(data)
            except OSError:
                pass
            finally:
                with self.lock:
                    if self.twin is sock:
                        self.twin = None
                try: sock.close()
                except OSError: pass
                print("[bridge] twin connection lost — reconnecting…", flush=True)
                time.sleep(0.4)

    def _broadcast(self, data):
        with self.subs_lock:
            targets = list(self.subs)
        for cb in targets:
            try:
                cb(data)
            except Exception:
                self.remove(cb)

    def add(self, cb):
        with self.subs_lock:
            self.subs.add(cb)

    def remove(self, cb):
        with self.subs_lock:
            self.subs.discard(cb)

    def write(self, data):
        """Write subscriber input to the twin. Returns True if delivered."""
        with self.lock:
            sock = self.twin
        if not sock:
            return False
        try:
            sock.sendall(data)
            return True
        except OSError:
            return False

    def connected(self):
        with self.lock:
            return self.twin is not None


# ───────────────────────── HTTP / WebSocket framing ─────────────────────────
def ws_accept(key):
    return base64.b64encode(hashlib.sha1((key + WS_MAGIC).encode()).digest()).decode()


def recv_http_request(conn):
    """Read one HTTP request head. Return (method, path, headers) or None."""
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(1024)
        if not chunk:
            return None
        data += chunk
        if len(data) > 65536:
            return None
    head = data.split(b"\r\n\r\n", 1)[0].decode("latin1")
    lines = head.split("\r\n")
    try:
        method, path, _ = lines[0].split(" ", 2)
    except ValueError:
        return None
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    return method, path, headers


def ws_frame(payload, opcode=0x1):
    data = payload.encode() if isinstance(payload, str) else payload
    header = bytearray([0x80 | opcode])
    n = len(data)
    if n < 126:
        header.append(n)
    elif n < 65536:
        header.append(126); header += struct.pack(">H", n)
    else:
        header.append(127); header += struct.pack(">Q", n)
    return bytes(header) + data


def ws_reader(conn):
    buf = b""
    def need(n):
        nonlocal buf
        while len(buf) < n:
            chunk = conn.recv(4096)
            if not chunk:
                raise ConnectionError
            buf += chunk
        out, buf = buf[:n], buf[n:]
        return out
    while True:
        try:
            b0, b1 = need(2)
            opcode = b0 & 0x0F
            masked = b1 & 0x80
            length = b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", need(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", need(8))[0]
            mask = need(4) if masked else b"\x00\x00\x00\x00"
            payload = bytearray(need(length))
            for i in range(length):
                payload[i] ^= mask[i & 3]
            if opcode == 0x8:      # close
                return
            if opcode in (0x1, 0x2):
                yield bytes(payload)
            # ping/pong ignored (0x9/0xA)
        except (ConnectionError, OSError):
            return


def serve_static(conn, path, root):
    """Serve a file from `root`. Path traversal is rejected."""
    rel = urllib.parse.urlparse(path).path
    rel = urllib.parse.unquote(rel)
    if rel in EXTRA_FILES:
        repo_root = os.path.realpath(os.path.join(root, "..", ".."))   # root is web/public → repo root
        full = os.path.realpath(os.path.join(repo_root, EXTRA_FILES[rel]))
        if full.startswith(repo_root + os.sep) and os.path.isfile(full):
            ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
            with open(full, "rb") as f:
                body = f.read()
            conn.sendall(("HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                          "Cache-Control: no-store\r\nConnection: close\r\n\r\n"
                          % (ctype, len(body))).encode() + body)
            return
    if rel == "/" or rel == "":
        rel = "/" + INDEX
    clean = posixpath.normpath(rel).lstrip("/")
    full = os.path.realpath(os.path.join(root, clean))
    root_real = os.path.realpath(root)
    if not (full == root_real or full.startswith(root_real + os.sep)) or not os.path.isfile(full):
        body = b"404 not found"
        conn.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n"
                     b"Content-Length: %d\r\nConnection: close\r\n\r\n" % len(body) + body)
        return
    ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
    with open(full, "rb") as f:
        body = f.read()
    conn.sendall(("HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                  "Cache-Control: no-store\r\nConnection: close\r\n\r\n"
                  % (ctype, len(body))).encode() + body)


def ws_relay(conn, headers, hub):
    """Attach a browser WebSocket to the hub (one of possibly many subscribers)."""
    accept = ws_accept(headers["sec-websocket-key"])
    conn.sendall(("HTTP/1.1 101 Switching Protocols\r\n"
                  "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())
    send_lock = threading.Lock()
    def on_twin(data):
        with send_lock:
            conn.sendall(ws_frame(data.decode("utf-8", "replace")))
    hub.add(on_twin)
    if not hub.connected():
        try: conn.sendall(ws_frame("[bridge] waiting for the twin… (start it with ./qemu/studio.sh)\n"))
        except OSError: pass
    print("[bridge] browser subscribed", flush=True)
    try:
        for payload in ws_reader(conn):
            hub.write(payload)
    finally:
        hub.remove(on_twin)
        try: conn.close()
        except OSError: pass
    print("[bridge] browser unsubscribed", flush=True)


def raw_console(conn, hub):
    """Attach a raw TCP console (./qemu/console.sh) to the hub."""
    send_lock = threading.Lock()
    def on_twin(data):
        with send_lock:
            conn.sendall(data)
    hub.add(on_twin)
    if not hub.connected():
        try: conn.sendall(b"[bridge] waiting for the twin...\n")
        except OSError: pass
    print("[bridge] console subscribed", flush=True)
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            hub.write(data)
    except OSError:
        pass
    finally:
        hub.remove(on_twin)
        try: conn.close()
        except OSError: pass
    print("[bridge] console unsubscribed", flush=True)


# ───────────────────────── Modbus/TCP proxy (real frames on :502) ─────────────────────────
# The browser can't open a raw TCP Modbus socket, so the bridge issues the transaction to the
# twin's real W5500-bridged Modbus server (127.0.0.1:502) and returns the request + response
# bytes. The transaction is a GENUINE packet on `lo` — capturable with `tshark -i lo port 502`.
import json as _json, time as _time, urllib.parse as _up

MODBUS_TARGET = os.environ.get("MODBUS_TARGET", "127.0.0.1:502")
_mb_tid = [0]

def _modbus_txn(target, pdu, timeout=3.0):
    host, port = target.rsplit(":", 1)
    _mb_tid[0] = (_mb_tid[0] + 1) & 0xFFFF
    tid = _mb_tid[0]
    adu = struct.pack(">HHHB", tid, 0, len(pdu) + 1, 1) + pdu
    s = socket.create_connection((host, int(port)), timeout=timeout)
    try:
        s.sendall(adu)
        hdr = b""
        while len(hdr) < 6:
            c = s.recv(6 - len(hdr))
            if not c: raise ConnectionError("short MBAP header")
            hdr += c
        ln = struct.unpack(">HHH", hdr)[2]
        body = b""
        while len(body) < ln:
            c = s.recv(ln - len(body))
            if not c: raise ConnectionError("short MBAP body")
            body += c
    finally:
        s.close()
    return adu, hdr + body

def _modbus_query(qs):
    """qs: parsed query dict. Returns a JSON-able result dict."""
    def g(k, d=None): return qs.get(k, [d])[0]
    fc = int(g("fc", "3"))
    addr = int(g("addr", "0"))
    count = int(g("count", "1"))
    value = int(g("value", "0"))
    if fc in (1, 2):      # read coils / discrete inputs
        pdu = struct.pack(">BHH", fc, addr, count)
    elif fc in (3, 4):    # read holding / input registers
        pdu = struct.pack(">BHH", fc, addr, count)
    elif fc == 5:         # write single coil
        pdu = struct.pack(">BHH", 5, addr, 0xFF00 if value else 0x0000)
    elif fc == 6:         # write single register
        pdu = struct.pack(">BHH", 6, addr, value & 0xFFFF)
    else:
        return {"ok": False, "error": "unsupported function code %d" % fc}
    t0 = _time.time()
    try:
        req, resp = _modbus_txn(MODBUS_TARGET, pdu)
    except Exception as e:
        return {"ok": False, "fc": fc, "addr": addr, "error": "modbus transaction failed: %s" % e,
                "target": MODBUS_TARGET}
    ms = round((_time.time() - t0) * 1000, 1)
    rpdu = resp[7:]   # after MBAP(6)+unit(1)
    out = {"ok": True, "fc": fc, "addr": addr, "count": count, "target": MODBUS_TARGET,
           "req_hex": req.hex(), "resp_hex": resp.hex(), "ms": ms, "values": [], "exception": None}
    if rpdu and (rpdu[0] & 0x80):          # modbus exception
        out["ok"] = False
        out["exception"] = rpdu[1] if len(rpdu) > 1 else None
    elif fc in (1, 2):
        nb = rpdu[1] if len(rpdu) > 1 else 0
        bits = []
        for i in range(count):
            byte = rpdu[2 + (i // 8)] if 2 + (i // 8) < len(rpdu) else 0
            bits.append((byte >> (i % 8)) & 1)
        out["values"] = bits
    elif fc in (3, 4):
        nb = rpdu[1] if len(rpdu) > 1 else 0
        regs = []
        for i in range(nb // 2):
            regs.append(struct.unpack(">H", rpdu[2 + i*2:4 + i*2])[0])
        out["values"] = regs
    elif fc in (5, 6):
        out["values"] = [value]
        out["wrote"] = True
    return out

def _serve_modbus(conn, path):
    qs = _up.parse_qs(_up.urlparse(path).query)
    try:
        result = _modbus_query(qs)
    except Exception as e:
        result = {"ok": False, "error": str(e)}
    body = _json.dumps(result).encode()
    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                 b"Cache-Control: no-store\r\n"
                 b"Content-Length: %d\r\nConnection: close\r\n\r\n" % len(body) + body)


def _serve_field(conn, method, path):
    """Chip-level I/O for the circuit views: the twin's AW9523 `di` byte (the 8 opto inputs as the
    expander sees them) and `relays` byte. GET reads both. POST ?bit=N&v=0|1 sets one input bit
    (read-modify-write under a lock) or POST ?di=<0-255> sets the whole byte. The firmware then
    reads the change over the emulated I2C bus on its next scan, exactly as if a wire had moved."""
    qs = _up.parse_qs(_up.urlparse(path).query)
    try:
        if method == "POST":
            if "di" in qs:
                di = int(qs["di"][0]) & 0xFF
            elif "bit" in qs:
                bit = int(qs["bit"][0]); v = int(qs.get("v", ["1"])[0])
                if not 0 <= bit <= 7:
                    raise ValueError("bit must be 0-7")
                cur = _qmp([("qom-get", {"path": _FIELD_PATH, "property": "di"})])[0]
                di = (cur | (1 << bit)) if v else (cur & ~(1 << bit) & 0xFF)
            else:
                raise ValueError("POST /field needs ?bit=N&v=0|1 or ?di=<byte>")
            _qmp([("qom-set", {"path": _FIELD_PATH, "property": "di", "value": di})])
        di, relays = _qmp([("qom-get", {"path": _FIELD_PATH, "property": "di"}),
                           ("qom-get", {"path": _FIELD_PATH, "property": "relays"})])
        result = {"ok": True, "di": di, "relays": relays,
                  "path": "QMP qom-get/qom-set %s (twin only; a physical board has no QMP)" % _FIELD_PATH}
    except FileNotFoundError:
        result = {"ok": False, "error": "no QMP socket at %s (is this a twin? a physical board has none)" % QMP_SOCK}
    except Exception as e:
        result = {"ok": False, "error": "%s: %s" % (type(e).__name__, e)}
    body = _json.dumps(result).encode()
    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
                 b"Content-Length: %d\r\nConnection: close\r\n\r\n" % len(body) + body)


def handle(conn, hub, root):
    try:
        req = recv_http_request(conn)
        if not req:
            conn.close(); return
        method, path, headers = req
        if headers.get("upgrade", "").lower() == "websocket" and "sec-websocket-key" in headers:
            if not origin_ok(headers):
                print("[bridge] refused console WebSocket from origin %r" % headers.get("origin"), flush=True)
                _deny(conn, 403, "cross-origin console connection refused"); conn.close(); return
            ws_relay(conn, headers, hub)
        elif path.split("?", 1)[0] == "/modbus":
            fc = _up.parse_qs(_up.urlparse(path).query).get("fc", ["3"])[0]
            write = fc in ("5", "6")
            if method not in ("GET", "HEAD", "POST"):
                _deny(conn, 405, "use GET to read or POST to write")
            elif write and method != "POST":
                _deny(conn, 405, "Modbus writes (fc=5/6) need POST")
            elif method == "POST" and not origin_ok(headers):
                print("[bridge] refused Modbus write from origin %r" % headers.get("origin"), flush=True)
                _deny(conn, 403, "cross-origin write refused")
            else:
                _serve_modbus(conn, path)
            conn.close()
        elif path.split("?", 1)[0] == "/field":
            if method == "POST" and not origin_ok(headers):
                print("[bridge] refused /field write from origin %r" % headers.get("origin"), flush=True)
                _deny(conn, 403, "cross-origin write refused")
            elif method in ("GET", "HEAD", "POST"):
                _serve_field(conn, method, path)
            else:
                _deny(conn, 405, "use GET to read or POST to write")
            conn.close()
        elif method in ("GET", "HEAD"):
            serve_static(conn, path, root)
            conn.close()
        else:
            conn.sendall(b"HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n")
            conn.close()
    except OSError:
        try: conn.close()
        except OSError: pass


def console_server(port, hub):
    """Localhost-only raw TCP console port — what ./qemu/console.sh attaches to."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind(("127.0.0.1", port)); srv.listen(8)
    except OSError as e:
        print("[bridge] console port %d unavailable (%s) — console.sh will use the socket directly" % (port, e), flush=True)
        return
    print("[bridge] text console: nc 127.0.0.1 %d   (./qemu/console.sh)" % port, flush=True)
    while True:
        try:
            conn, _ = srv.accept()
        except OSError:
            return
        threading.Thread(target=raw_console, args=(conn, hub), daemon=True).start()


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Serve the Logic Studio and bridge it to the QEMU twin, on one port.")
    ap.add_argument("--port", type=int, default=int(os.environ.get("BRIDGE_PORT", "8080")))
    ap.add_argument("--console-port", type=int, default=int(os.environ.get("BRIDGE_CONSOLE_PORT", "8081")),
                    help="localhost raw TCP console port for ./qemu/console.sh (0 disables)")
    ap.add_argument("--serial", default=os.environ.get("SERIAL_SOCK", "/tmp/plc-serial"),
                    help="twin console: a unix socket path, or host:port for TCP")
    ap.add_argument("--modbus", default=os.environ.get("MODBUS_TARGET", "127.0.0.1:502"),
                    help="twin Modbus/TCP endpoint the /modbus proxy talks to (host:port)")
    ap.add_argument("--dir", default=os.environ.get("STUDIO_DIR", os.path.join(here, "..", "web", "public")),
                    help="static web root to serve (default ../web/public)")
    ap.add_argument("--bind", default=os.environ.get("BRIDGE_BIND", "127.0.0.1"),
                    help="address the Studio port listens on (default 127.0.0.1)")
    ap.add_argument("--qmp", default=os.environ.get("QMP_SOCK", "/tmp/plc-qmp"),
                    help="the twin's QMP socket, used only by the whitelisted /field endpoint")
    a = ap.parse_args(argv)
    root = os.path.realpath(a.dir)
    global MODBUS_TARGET, QMP_SOCK
    MODBUS_TARGET = a.modbus
    QMP_SOCK = a.qmp

    hub = TwinHub(a.serial)

    if a.console_port:
        threading.Thread(target=console_server, args=(a.console_port, hub), daemon=True).start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((a.bind, a.port)); srv.listen(16)
    print("twin-bridge: http://localhost:%d  (Studio, listening on %s)   ws:///ws  ->  %s"
          % (a.port, a.bind, a.serial), flush=True)
    print("Open that URL (in a Codespace, the forwarded port %d) and click 'Connect twin'." % a.port, flush=True)
    if not os.path.isfile(os.path.join(root, INDEX)):
        print("  WARNING: %s not found under %s — static serving will 404." % (INDEX, root), flush=True)
    try:
        while True:
            conn, _ = srv.accept()
            threading.Thread(target=handle, args=(conn, hub, root), daemon=True).start()
    except KeyboardInterrupt:
        print("\ntwin-bridge: stopped", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
