#!/usr/bin/env python3
"""twin-bridge — serve the Logic Studio AND bridge it to the QEMU twin, on one port.

The Studio is a static web page; the twin's console is a local socket, not a USB
device, so the browser can't reach it over Web Serial. This one server solves both
halves at once:

  * it serves the Studio (and the rest of web/public) over HTTP, and
  * it upgrades a WebSocket on /ws and relays it to the twin's console socket.

Because the page and the socket share one origin, the Studio's "Connect twin" URL is
just same-origin /ws — correct in a Codespace (one forwarded port, no host:port
guessing) and on a laptop (localhost) alike. One student, one port, no teams, no
hardware, no Netlify.

    ┌──────────┐  http (page) + ws:///ws   ┌──────────────┐  unix/tcp socket  ┌──────┐
    │ browser  │ ◀───── one origin ──────▶ │  twin-bridge  │ ────────────────▶ │ twin │
    └──────────┘       :8080               └──────────────┘   /tmp/plc-serial  └──────┘

Dependency-free (Python 3 stdlib only), so it runs in a Codespace or a laptop with
no `pip install`.

    ./qemu/run.sh &                 # start the twin (console on /tmp/plc-serial)
    ./qemu/twin-bridge.py           # open http://localhost:8080  →  click "Connect twin"

Options (env or flags):
    --port 8080                WebSocket + HTTP port (default 8080)
    --serial /tmp/plc-serial   twin console: a unix socket path, or host:port for TCP
    --dir <path>               static web root (default: ../web/public next to this script)
"""
import argparse, base64, hashlib, mimetypes, os, posixpath, socket, struct, sys, threading
import urllib.parse

WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
INDEX = "logic_studio.html"   # what "/" serves


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


def open_twin(serial):
    if ":" in serial and not os.path.exists(serial):
        host, port = serial.rsplit(":", 1)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((host, int(port)))
    else:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(serial)
    return s


def serve_static(conn, path, root):
    """Serve a file from `root`. Path traversal is rejected."""
    rel = urllib.parse.urlparse(path).path
    rel = urllib.parse.unquote(rel)
    if rel == "/" or rel == "":
        rel = "/" + INDEX
    # normalize and confine to root
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


def ws_relay(conn, headers, serial):
    accept = ws_accept(headers["sec-websocket-key"])
    conn.sendall(("HTTP/1.1 101 Switching Protocols\r\n"
                  "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())
    try:
        twin = open_twin(serial)
    except OSError as e:
        conn.sendall(ws_frame("[bridge] cannot reach twin at %s: %s\n" % (serial, e)))
        conn.close(); return
    print("[bridge] browser <-> twin (%s) connected" % serial, flush=True)
    alive = [True]

    def twin_to_ws():
        try:
            while alive[0]:
                data = twin.recv(4096)
                if not data:
                    break
                conn.sendall(ws_frame(data.decode("utf-8", "replace")))
        except OSError:
            pass
        finally:
            alive[0] = False
            try: conn.close()
            except OSError: pass

    t = threading.Thread(target=twin_to_ws, daemon=True); t.start()
    try:
        for payload in ws_reader(conn):
            twin.sendall(payload)
    finally:
        alive[0] = False
        try: twin.close()
        except OSError: pass
        try: conn.close()
        except OSError: pass
    print("[bridge] connection closed", flush=True)


def handle(conn, serial, root):
    try:
        req = recv_http_request(conn)
        if not req:
            conn.close(); return
        method, path, headers = req
        if headers.get("upgrade", "").lower() == "websocket" and "sec-websocket-key" in headers:
            ws_relay(conn, headers, serial)
        elif method in ("GET", "HEAD"):
            serve_static(conn, path, root)
            conn.close()
        else:
            conn.sendall(b"HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n")
            conn.close()
    except OSError:
        try: conn.close()
        except OSError: pass


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Serve the Logic Studio and bridge it to the QEMU twin, on one port.")
    ap.add_argument("--port", type=int, default=int(os.environ.get("BRIDGE_PORT", "8080")))
    ap.add_argument("--serial", default=os.environ.get("SERIAL_SOCK", "/tmp/plc-serial"),
                    help="twin console: a unix socket path, or host:port for TCP")
    ap.add_argument("--dir", default=os.environ.get("STUDIO_DIR", os.path.join(here, "..", "web", "public")),
                    help="static web root to serve (default ../web/public)")
    a = ap.parse_args(argv)
    root = os.path.realpath(a.dir)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", a.port)); srv.listen(16)
    print("twin-bridge: http://localhost:%d  (Studio)   ws:///ws  ->  %s" % (a.port, a.serial), flush=True)
    print("Open that URL (in a Codespace, the forwarded port 8080) and click 'Connect twin'.", flush=True)
    if not os.path.isfile(os.path.join(root, INDEX)):
        print("  WARNING: %s not found under %s — static serving will 404." % (INDEX, root), flush=True)
    try:
        while True:
            conn, _ = srv.accept()
            threading.Thread(target=handle, args=(conn, a.serial, root), daemon=True).start()
    except KeyboardInterrupt:
        print("\ntwin-bridge: stopped", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
