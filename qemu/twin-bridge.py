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
"""
import argparse, base64, hashlib, mimetypes, os, posixpath, socket, struct, sys, threading, time
import urllib.parse

WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
INDEX = "logic_studio.html"   # what "/" serves


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


def handle(conn, hub, root):
    try:
        req = recv_http_request(conn)
        if not req:
            conn.close(); return
        method, path, headers = req
        if headers.get("upgrade", "").lower() == "websocket" and "sec-websocket-key" in headers:
            ws_relay(conn, headers, hub)
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
    ap.add_argument("--dir", default=os.environ.get("STUDIO_DIR", os.path.join(here, "..", "web", "public")),
                    help="static web root to serve (default ../web/public)")
    a = ap.parse_args(argv)
    root = os.path.realpath(a.dir)

    hub = TwinHub(a.serial)

    if a.console_port:
        threading.Thread(target=console_server, args=(a.console_port, hub), daemon=True).start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", a.port)); srv.listen(16)
    print("twin-bridge: http://localhost:%d  (Studio)   ws:///ws  ->  %s" % (a.port, a.serial), flush=True)
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
