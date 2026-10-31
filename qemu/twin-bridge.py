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
  * POST /bus/capture?seconds=N (1-10) records the student's OWN running twin's I2C bus: it sends a
    fixed sequence of QMP commands — `logfile <fixed path>`, `trace-event-set-state` for the
    whitelisted I2C observe event only, then (after N s) the same event off and `logfile /dev/null`
    — and converts the trace with bus-analysis/tools/trace_to_sigrok.py. No query parameter
    reaches QMP except the clamped duration; tracing is always switched off again (finally:).
    GET /bus/files/<name> downloads results (strict name pattern); GET /bus/captures lists them.
  * GET /fw/api/log and GET /fw/api/points are a read-only proxy to the firmware's own HTTP HMI
    (the twin's W5500, guest port 8080) — exactly those two paths, GET only, no query passthrough.
  * GET /twin/identity reads the running twin's flash image (partition table, otadata, app
    descriptor): profile, boot slot, ESP-IDF version, app ELF SHA-256. Read-only.
  Clients without an Origin header (curl, scripts, the console) are not browsers and are allowed.
"""
import argparse, base64, hashlib, mimetypes, os, posixpath, re, socket, struct, sys, threading, time
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
        if method == "POST":
            _bus_note_field(di)                    # an open bus capture draws it as IN1..IN4
        result = {"ok": True, "di": di, "relays": relays,
                  "path": "QMP qom-get/qom-set %s (twin only; a physical board has no QMP)" % _FIELD_PATH}
    except FileNotFoundError:
        result = {"ok": False, "error": "no QMP socket at %s (is this a twin? a physical board has none)" % QMP_SOCK}
    except Exception as e:
        result = {"ok": False, "error": "%s: %s" % (type(e).__name__, e)}
    body = _json.dumps(result).encode()
    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
                 b"Content-Length: %d\r\nConnection: close\r\n\r\n" % len(body) + body)


def _send_json(conn, result, code=200):
    body = _json.dumps(result).encode()
    reason = {200: b"OK", 400: b"Bad Request", 404: b"Not Found", 409: b"Conflict",
              502: b"Bad Gateway", 503: b"Service Unavailable"}.get(code, b"OK")
    conn.sendall(b"HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
                 b"Content-Length: %d\r\nConnection: close\r\n\r\n" % (code, reason, len(body)) + body)


# ───────────────────────── the running twin: process, image, identity ─────────────────────────
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import fw_identity as _fwid
except Exception:                                   # keep the Studio serving even if this is missing
    _fwid = None


def _twin_process():
    """The qemu process serving this bridge's QMP socket: {pid, image, exe, env}. None if absent."""
    want = "unix:%s,server" % QMP_SOCK
    try:
        pids = [p for p in os.listdir("/proc") if p.isdigit()]
    except OSError:
        return None
    for pid in pids:
        try:
            with open("/proc/%s/cmdline" % pid, "rb") as f:
                args = f.read().split(b"\0")
        except OSError:
            continue
        a = [x.decode("utf-8", "replace") for x in args if x]
        if not a or "qemu-system-xtensa" not in a[0] or want not in " ".join(a):
            continue
        img = None
        for i, x in enumerate(a[:-1]):
            if x == "-drive" and "if=mtd" in a[i + 1]:
                for part in a[i + 1].split(","):
                    if part.startswith("file="):
                        img = part[5:]
        env = {}
        try:
            with open("/proc/%s/environ" % pid, "rb") as f:
                for kv in f.read().split(b"\0"):
                    if b"=" in kv:
                        k, v = kv.split(b"=", 1)
                        if k.endswith(b"_W5_BIND"):
                            env[k.decode()] = v.decode("utf-8", "replace")
        except OSError:
            pass
        exe = None
        try:
            exe = os.readlink("/proc/%s/exe" % pid)
        except OSError:
            exe = a[0]
        return {"pid": int(pid), "image": img, "exe": exe, "env": env}
    return None


_sha_cache = {}
def _sha256_cached(path):
    try:
        st = os.stat(path)
    except OSError:
        return None
    key = (path, st.st_size, st.st_mtime)
    if key not in _sha_cache:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for b in iter(lambda: f.read(1 << 20), b""):
                h.update(b)
        _sha_cache[key] = h.hexdigest()
    return _sha_cache[key]


def _twin_identity(with_hash=False):
    tw = _twin_process()
    if not tw:
        return {"ok": False, "error": "no running twin on QMP socket %s (start it: ./qemu/studio.sh)" % QMP_SOCK}
    out = {"ok": True, "pid": tw["pid"], "w5_bind": next(iter(tw["env"].values()), None)}
    if tw["image"] and _fwid and os.path.isfile(tw["image"]):
        info = _fwid.identify(tw["image"], with_hash=with_hash)
        info["image"] = os.path.relpath(info["image"], REPO_ROOT) if info["image"].startswith(REPO_ROOT + os.sep) else info["image_name"]
        out.update(info)
    else:
        out["image"] = tw["image"]
        out["warning"] = "could not read the twin's flash image"
    if with_hash and tw["exe"]:
        out["qemu_sha256"] = _sha256_cached(tw["exe"])
    return out


# ───────────────────────── read-only proxy to the firmware's HTTP HMI ─────────────────────────
FW_HTTP = os.environ.get("FW_HTTP", "")              # host:port; default derived from the twin
FW_HMI_PORT = 8080                                   # WEB_HMI_PORT in sbom/firmware/src/config.h
FW_PROXY_PATHS = ("/fw/api/log", "/fw/api/points")
_BRIDGE_ADDR = {"bind": "127.0.0.1", "port": 8080}


def _fw_target():
    if FW_HTTP:
        return FW_HTTP, "FW_HTTP"
    tw = _twin_process()
    bind = (next(iter(tw["env"].values()), "") if tw else "") or "127.0.0.1"
    if bind in ("0.0.0.0", "any"):
        bind = "127.0.0.1"
    if bind in ("lo", "loopback"):
        bind = "127.0.0.1"
    return "%s:%d" % (bind, FW_HMI_PORT), "twin W5500 bind"


def _shadowed(target):
    host, port = target.rsplit(":", 1)
    if int(port) != _BRIDGE_ADDR["port"]:
        return False
    b = _BRIDGE_ADDR["bind"]
    return b in ("0.0.0.0", "", host) or (b.startswith("127.") and host.startswith("127.") and b == host)


def _serve_fw_proxy(conn, path):
    target, why = _fw_target()
    sub = path.split("?", 1)[0][3:]                  # /fw/api/log -> /api/log (no query passthrough)
    if _shadowed(target):
        return _send_json(conn, {
            "ok": False, "shadowed": True, "target": target,
            "error": "the firmware's HTTP HMI (guest port %d) binds %s, which this Studio server already "
                     "holds, so the firmware page is not reachable from the host" % (FW_HMI_PORT, target),
            "fallback": "read the same audit log over the console: plc> log (Provenance Studio does this "
                        "automatically through the Studio's /ws console)",
            "fix": "run the Studio on another port so the firmware keeps 8080: "
                   "./qemu/twin-status.sh stop; BRIDGE_PORT=8088 ./qemu/studio.sh"}, 503)
    host, port = target.rsplit(":", 1)
    try:
        s = socket.create_connection((host, int(port)), timeout=4)
        s.settimeout(4)
        s.sendall(("GET %s HTTP/1.0\r\nHost: %s\r\nX-Studio-Proxy: 1\r\nConnection: close\r\n\r\n"
                   % (sub, target)).encode())
        data = b""
        while len(data) < 2 * 1024 * 1024:
            c = s.recv(65536)
            if not c:
                break
            data += c
        s.close()
        head, _, body = data.partition(b"\r\n\r\n")
        status = head.split(b"\r\n", 1)[0].decode("latin1", "replace")
        parts = status.split(" ")
        if len(parts) < 2 or parts[1] != "200":
            return _send_json(conn, {"ok": False, "target": target, "error": "firmware answered: " + status}, 502)
        conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
                     b"X-Source: firmware HTTP " + target.encode() + b"\r\n"
                     b"Content-Length: %d\r\nConnection: close\r\n\r\n" % len(body) + body)
    except OSError as e:
        _send_json(conn, {"ok": False, "target": target, "via": why,
                          "error": "firmware HTTP not reachable at %s (%s) - is the twin up and its HTTP "
                                   "service on?  plc> proto" % (target, e)}, 502)


# ───────────────────────── live I2C capture of the running twin ─────────────────────────
OBS_EVENT = "esp32_i2c_observe"                      # released twin (qemu-twin-v1)
GENERIC_EVENTS = ("i2c_event", "i2c_send", "i2c_recv")   # fallback for other builds
BUS_OUT_DIR = os.environ.get("BUS_OUT_DIR", os.path.join(REPO_ROOT, "bus-analysis", "out"))
BUS_TRACE_FILE = os.environ.get("BUS_TRACE_FILE", "/tmp/plc-bustap-%d.log" % os.getuid())
BUS_MAX_SECONDS = 10
BUS_MAX_TRACE = 256 * 1024 * 1024
STUDIO_MAX_EVENTS = 50000
STUDIO_EVENTS = {"reset", "fsm_reset", "address", "write", "read", "stop", "rstart_command", "end_command",
                 "ack_mismatch_stop", "tx_wait", "rx_wait", "invalid_write_count", "invalid_opcode"}
_FILE_RE = re.compile(r"^bus-\d{8}-\d{6}-i2c[01]\.(vcd|sr|pvs|json|studio\.json|decode\.txt|"
                                    r"manifest\.json|trace\.log\.gz)$")
_bus_lock = threading.Lock()
_bus_tap = {"active": False, "writes": []}


def _bus_note_field(di):
    if _bus_tap["active"]:
        _bus_tap["writes"].append({"t": time.time(), "di": int(di)})


def _hmp(cmd):
    """One FIXED human-monitor command (only the two logfile forms below are ever sent)."""
    r = _qmp([("human-monitor-command", {"command-line": cmd})])[0]
    if isinstance(r, str) and r.strip():
        raise RuntimeError("monitor: " + r.strip())


def _load_converter():
    import importlib.util
    p = os.path.join(REPO_ROOT, "bus-analysis", "tools", "trace_to_sigrok.py")
    spec = importlib.util.spec_from_file_location("trace_to_sigrok", p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def _sigrok_summary(src, out_txt):
    import shutil as _sh, subprocess as _sp
    _re = re
    if not _sh.which("sigrok-cli"):
        return {"ok": False, "error": "sigrok-cli not installed (sudo apt-get install sigrok-cli)"}
    r = _sp.run(["sigrok-cli", "-i", src, "-P", "i2c:scl=SCL:sda=SDA",
                 "-A", "i2c=start:repeat-start:stop:address-read:address-write:data-read:data-write",
                 "--protocol-decoder-samplenum"], capture_output=True, text=True, timeout=180)
    with open(out_txt, "w") as f:
        f.write(r.stdout)
    addrs, changes, events = {}, [], []
    last = {}
    cur_addr, cur_dir, pointer, nbytes = None, None, None, 0
    for line in r.stdout.splitlines():
        m = _re.match(r"^(\d+)-(\d+) i2c-\d+: (.*)$", line)
        if not m:
            continue
        s0, ann = int(m.group(1)), m.group(3)
        if ann.startswith("Address "):
            k = ann.replace("Address ", "")
            addrs[k] = addrs.get(k, 0) + 1
            cur_dir, cur_addr = k.split(": ")[0], k.split(": ")[1].lower()
            nbytes, t_frame = 0, s0
        elif ann.startswith("Data ") and cur_addr == "59":
            v = ann.split(": ")[1].lower()
            if cur_dir == "write":
                if nbytes == 0:
                    pointer = v
                else:
                    reg = "%02x" % ((int(pointer, 16) + nbytes - 1) & 0xFF)
                    if last.get(("W", reg)) != v:
                        last[("W", reg)] = v
                        events.append({"t_s": round(s0 / 4e6, 6), "op": "write", "reg": "0x" + reg, "value": "0x" + v})
                        if reg == "02":
                            changes.append({"t_s": round(s0 / 4e6, 6), "value": "0x" + v})
            else:
                reg = "%02x" % ((int(pointer or "0", 16) + nbytes) & 0xFF)
                if last.get(("R", reg)) != v:
                    last[("R", reg)] = v
                    events.append({"t_s": round(s0 / 4e6, 6), "op": "read", "reg": "0x" + reg, "value": "0x" + v})
            nbytes += 1
    return {"ok": r.returncode == 0, "decoder": "sigrok-cli i2c", "addresses": addrs,
            "relay_write_changes_0x59_reg02": changes, "changes_0x59": events[:400],
            "file": os.path.basename(out_txt),
            "time_base": "sample number / 4 MHz (drawn timeline)"}


def _write_pvs(path, channels):
    """PulseView session setup beside the .sr (same base name), stacking the I2C decoder on SCL/SDA.
    PulseView restores <name>.pvs when it opens <name>.sr. Best effort: if a PulseView version
    ignores it, Add decoder -> I2C (SCL=SCL, SDA=SDA) by hand."""
    lines = ["[General]", "decode_signals=1",
             "", "[decode_signal0]", "name=I2C (live tap)", "enabled=true", "decoders=1",
             "decoder0\\id=i2c", "decoder0\\visible=true", "decoder0\\options=0",
             "channels=2",
             "channel0\\name=SCL", "channel0\\initial_pin_state=2", "channel0\\assigned_signal_name=SCL",
             "channel1\\name=SDA", "channel1\\initial_pin_state=2", "channel1\\assigned_signal_name=SDA", ""]
    with open(path, "w") as f:
        f.write("\n".join(lines))


def _studio_bundle(obs_lines, identity, session, t_start, t_end):
    _re = re
    trace, mapping, n, cut = [], {}, 0, False
    for ln in obs_lines:
        body = ln.split(":", 1)[1] if _re.match(r"^\d+@\d+\.\d+:", ln) else ln
        m = _re.search(r"controller=(0x[0-9a-f]+) path=(\S+) .* event=(\S+) ", body)
        if not m:
            continue
        if m.group(3) not in STUDIO_EVENTS:
            return None, "event %r is outside the Studio importer's vocabulary" % m.group(3)
        if n >= STUDIO_MAX_EVENTS:
            cut = True
            break
        mapping[m.group(1)] = m.group(2)
        trace.append(body)
        n += 1
    if not trace:
        return None, "no observe events"
    text = "\n".join(trace) + "\n"
    if len(text.encode()) > 16 * 1024 * 1024:
        return None, "trace larger than the Studio's 16 MiB import limit"
    manifest = {"schema": "qemu-i2c-observation-capture/v1", "source_kind": "qemu-peripheral-transactions",
                "machine": "esp32s3", "session": session, "records": n,
                "identity": identity, "files": {"trace.log": hashlib.sha256(text.encode()).hexdigest()},
                "controller_mapping": mapping, "captured_wall": [t_start, t_end],
                "origin": "live capture of the student's running twin via the Studio server (/bus/capture)",
                "truncated_to_first_events": STUDIO_MAX_EVENTS if cut else None}
    return {"schema": "studio-qemu-bus/v1", "manifest": manifest, "trace": text}, ("first %d events only" % n if cut else None)


def _bus_capture(qs):
    import gzip, uuid
    try:
        secs = max(1, min(BUS_MAX_SECONDS, int((qs.get("seconds") or qs.get("secs") or ["6"])[0])))
    except ValueError:
        return {"ok": False, "error": "seconds must be an integer 1-%d" % BUS_MAX_SECONDS}, 400
    bus = qs.get("bus", ["i2c0"])[0]
    if bus not in ("i2c0", "i2c1"):
        return {"ok": False, "error": "bus must be i2c0 (relays/DI/sensors) or i2c1 (Grove)"}, 400
    if not os.path.exists(QMP_SOCK):
        return {"ok": False, "error": "no QMP socket at %s - start the twin with ./qemu/studio.sh" % QMP_SOCK}, 503
    if not _bus_lock.acquire(blocking=False):
        return {"ok": False, "error": "a bus capture is already running - wait for it to finish"}, 409
    try:
        try:
            _qmp([("trace-event-get-state", {"name": OBS_EVENT})])
            names, fmt = [OBS_EVENT], "observe"
        except RuntimeError:
            names, fmt = [], "generic"
            for n in GENERIC_EVENTS:
                try:
                    _qmp([("trace-event-get-state", {"name": n})]); names.append(n)
                except RuntimeError:
                    pass
            if not names:
                return {"ok": False, "error": "this twin build has no I2C trace events"}, 503
        di0, rel0 = _qmp([("qom-get", {"path": _FIELD_PATH, "property": "di"}),
                          ("qom-get", {"path": _FIELD_PATH, "property": "relays"})])
        try:
            os.unlink(BUS_TRACE_FILE)
        except FileNotFoundError:
            pass
        _hmp("logfile " + BUS_TRACE_FILE)
        t_start = time.time()
        _bus_tap.update(active=True, writes=[], di_start=di0)
        off_error = None
        try:
            _qmp([("trace-event-set-state", {"name": n, "enable": True}) for n in names])
            time.sleep(secs)
        finally:
            _bus_tap["active"] = False
            for attempt in range(3):              # tracing must never stay on
                try:
                    _qmp([("trace-event-set-state", {"name": n, "enable": False}) for n in names])
                    _hmp("logfile /dev/null")
                    off_error = None
                    break
                except Exception as e:            # noqa: BLE001
                    off_error = str(e); time.sleep(0.3)
        t_end = time.time()
        di1, rel1 = _qmp([("qom-get", {"path": _FIELD_PATH, "property": "di"}),
                          ("qom-get", {"path": _FIELD_PATH, "property": "relays"})])
        truncated = False
        lines = []
        try:
            with open(BUS_TRACE_FILE, "r", errors="replace") as f:
                size = 0
                for ln in f:
                    size += len(ln)
                    if size > BUS_MAX_TRACE:
                        truncated = True
                        break
                    if "esp32_i2c_observe" in ln or ln.split(":", 1)[-1].startswith("i2c_"):
                        lines.append(ln.rstrip("\n"))
        finally:
            try:
                os.unlink(BUS_TRACE_FILE)
            except FileNotFoundError:
                pass
        os.makedirs(BUS_OUT_DIR, exist_ok=True)
        cid = "bus-%s-%s" % (time.strftime("%Y%m%d-%H%M%S", time.localtime(t_start)), bus)
        base = os.path.join(BUS_OUT_DIR, cid)
        T = _load_converter()
        field = {"di_start": di0, "writes": list(_bus_tap["writes"])}
        summ, txns = T.convert(lines, bus, base, "virtual", field, None, fmt)
        with gzip.open(base + ".trace.log.gz", "wt") as g:
            g.write("\n".join(lines) + "\n")
        files = {"vcd": cid + ".vcd", "json": cid + ".json", "trace": cid + ".trace.log.gz"}
        sig = {"ok": False, "error": "skipped"}
        try:
            import shutil as _sh, subprocess as _sp
            if _sh.which("sigrok-cli") and txns:
                r = _sp.run(["sigrok-cli", "-I", "vcd", "-i", base + ".vcd", "-o", base + ".sr"],
                            capture_output=True, text=True, timeout=180)
                if r.returncode == 0 and os.path.isfile(base + ".sr"):
                    files["sr"] = cid + ".sr"
                    _write_pvs(base + ".pvs", summ.get("channels", ["SCL", "SDA"]))
                    files["pvs"] = cid + ".pvs"
                sig = _sigrok_summary(base + (".sr" if "sr" in files else ".vcd"), base + ".decode.txt")
                files["decode"] = cid + ".decode.txt"
            elif not txns:
                sig = {"ok": False, "error": "nothing to decode (0 transactions)"}
            else:
                sig = {"ok": False, "error": "sigrok-cli not installed (sudo apt-get install sigrok-cli)"}
        except Exception as e:                    # noqa: BLE001
            sig = {"ok": False, "error": "sigrok-cli failed: %s" % e}
        ident = _twin_identity(with_hash=True)
        session = str(uuid.uuid4())
        bundle_note = None
        if fmt == "observe" and lines and ident.get("flash_sha256") and ident.get("qemu_sha256"):
            bundle, bundle_note = _studio_bundle(lines, {"qemu_sha256": ident["qemu_sha256"],
                                                         "flash_sha256": ident["flash_sha256"]},
                                                 session, t_start, t_end)
            if bundle:
                with open(base + ".studio.json", "w") as f:
                    _json.dump(bundle, f)
                files["studio"] = cid + ".studio.json"
        manifest = {
            "id": cid, "session": session, "bus": bus, "seconds": secs, "format": fmt, "trace_events": names,
            "captured": {"start": t_start, "end": t_end,
                         "start_local": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t_start))},
            "twin": {k: ident.get(k) for k in ("pid", "profile", "image", "selected_slot", "flash_sha256",
                                                "qemu_sha256", "w5_bind")},
            "firmware": ident.get("app"),
            "field": {"di_start": di0, "di_end": di1, "relays_start": rel0, "relays_end": rel1,
                      "writes": field["writes"],
                      "how": "chip-level /field writes during the capture, drawn as IN1..IN4 at the first "
                             "bus event logged after each write (host clock)"},
            "summary": summ, "sigrok": sig, "files": files,
            "studio_bundle_note": bundle_note, "trace_truncated": truncated, "tracing_off_error": off_error}
        with open(base + ".manifest.json", "w") as f:
            _json.dump(manifest, f, indent=1)
        files["manifest"] = cid + ".manifest.json"
        result = {"ok": bool(txns), **manifest,
                  "download": {k: "/bus/files/" + v for k, v in files.items()},
                  "repo_path": os.path.relpath(BUS_OUT_DIR, REPO_ROOT) if BUS_OUT_DIR.startswith(REPO_ROOT) else BUS_OUT_DIR}
        if not txns:
            result["error"] = "the trace held no I2C transactions for %s" % bus
        return result, 200
    except Exception as e:                        # noqa: BLE001
        return {"ok": False, "error": "%s: %s" % (type(e).__name__, e)}, 500
    finally:
        _bus_lock.release()


def _serve_bus_file(conn, path):
    name = urllib.parse.unquote(path.split("?", 1)[0][len("/bus/files/"):])
    full = os.path.join(BUS_OUT_DIR, name)
    if not _FILE_RE.match(name) or not os.path.isfile(full):
        return _send_json(conn, {"ok": False, "error": "no such capture file"}, 404)
    ctype = {"json": "application/json", "txt": "text/plain; charset=utf-8", "vcd": "text/plain"}.get(
        name.rsplit(".", 1)[-1], "application/octet-stream")
    with open(full, "rb") as f:
        body = f.read()
    conn.sendall(("HTTP/1.1 200 OK\r\nContent-Type: %s\r\nCache-Control: no-store\r\n"
                  "Content-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %d\r\n"
                  "Connection: close\r\n\r\n" % (ctype, name, len(body))).encode() + body)


def _bus_list():
    out = []
    try:
        names = sorted((n for n in os.listdir(BUS_OUT_DIR) if n.endswith(".manifest.json")), reverse=True)
    except OSError:
        names = []
    for n in names[:20]:
        try:
            with open(os.path.join(BUS_OUT_DIR, n)) as f:
                m = _json.load(f)
            out.append({"id": m.get("id"), "captured": m.get("captured", {}).get("start_local"),
                        "seconds": m.get("seconds"), "frames": m.get("summary", {}).get("frames"),
                        "relay_write_changes": m.get("summary", {}).get("relay_write_changes"),
                        "by_address": m.get("summary", {}).get("by_address"), "bus": m.get("bus"),
                        "firmware_elf_sha256": (m.get("firmware") or {}).get("elf_sha256"),
                        "download": {k: "/bus/files/" + v for k, v in m.get("files", {}).items()}})
        except Exception:                         # noqa: BLE001
            continue
    return {"ok": True, "captures": out}


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
        elif path.split("?", 1)[0] in ("/bus/capture", "/bustap"):          # /bustap?secs=N = alias
            if method != "POST":
                _deny(conn, 405, "use POST /bus/capture?seconds=6 (e.g. curl -X POST); results: GET /bus/captures")
            elif not origin_ok(headers):
                print("[bridge] refused bus capture from origin %r" % headers.get("origin"), flush=True)
                _deny(conn, 403, "cross-origin capture refused")
            else:
                result, code = _bus_capture(_up.parse_qs(_up.urlparse(path).query))
                _send_json(conn, result, code)
            conn.close()
        elif path.startswith("/bus/files/") and method in ("GET", "HEAD"):
            _serve_bus_file(conn, path); conn.close()
        elif path.split("?", 1)[0] == "/bus/captures" and method in ("GET", "HEAD"):
            _send_json(conn, _bus_list()); conn.close()
        elif path.split("?", 1)[0] == "/twin/identity" and method in ("GET", "HEAD"):
            qs = _up.parse_qs(_up.urlparse(path).query)
            _send_json(conn, _twin_identity(with_hash=qs.get("hash", ["0"])[0] == "1")); conn.close()
        elif path.split("?", 1)[0] in FW_PROXY_PATHS:
            if method not in ("GET", "HEAD"):
                _deny(conn, 405, "read-only: GET only")
            elif headers.get("x-studio-proxy"):
                _send_json(conn, {"ok": False, "error": "proxy loop"}, 502)
            else:
                _serve_fw_proxy(conn, path)
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
    ap.add_argument("--fw-http", default=os.environ.get("FW_HTTP", ""),
                    help="firmware HTTP HMI host:port for the read-only /fw/api/* proxy "
                         "(default: the twin's W5500 bind address, port 8080)")
    a = ap.parse_args(argv)
    root = os.path.realpath(a.dir)
    global MODBUS_TARGET, QMP_SOCK, FW_HTTP
    MODBUS_TARGET = a.modbus
    QMP_SOCK = a.qmp
    FW_HTTP = a.fw_http
    _BRIDGE_ADDR.update(bind=a.bind, port=a.port)

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
