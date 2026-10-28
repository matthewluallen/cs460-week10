#!/usr/bin/env python3
"""twin-bridge — let the browser Logic Studio drive the QEMU twin.

The Studio connects over Web Serial, which only reaches a physical USB device. The
twin's console is a local socket, not USB. This bridge exposes that console as a
WebSocket so the Studio's "Connect twin" button can drive the emulated controller
exactly as it would a real StampLC over USB — force inputs, read I/O, load a program.

    ┌──────────┐  Web Serial? no      ┌──────────────┐  unix/tcp socket  ┌──────┐
    │  Studio  │ ──ws://host:8081──▶  │  twin-bridge  │ ───────────────▶ │ twin │
    └──────────┘   (this script)      └──────────────┘   /tmp/plc-serial └──────┘

Dependency-free (Python 3 stdlib only), so it runs in a Codespace or a laptop with
no `pip install`. One browser at a time is plenty for a student.

    ./qemu/run.sh &                 # start the twin (console on /tmp/plc-serial)
    ./qemu/twin-bridge.py           # then open the Studio and click "Connect twin"

Options (env or flags):
    --port 8081             WebSocket port to listen on (default 8081)
    --serial /tmp/plc-serial   twin console: a unix socket path, or host:port for TCP
"""
import argparse, base64, hashlib, os, socket, struct, sys, threading

WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def ws_accept(key):
    return base64.b64encode(hashlib.sha1((key + WS_MAGIC).encode()).digest()).decode()


def recv_http_headers(conn):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = conn.recv(1024)
        if not chunk:
            return None
        data += chunk
        if len(data) > 65536:
            return None
    head = data.split(b"\r\n\r\n", 1)[0].decode("latin1")
    headers = {}
    for line in head.split("\r\n")[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    return headers


def ws_frame(payload, opcode=0x1):
    """Build a server->client frame (never masked). Text (0x1) by default."""
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
    """Yield decoded payloads from client frames; stop on close/error."""
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


def handle(conn, serial):
    headers = recv_http_headers(conn)
    if not headers or "sec-websocket-key" not in headers:
        conn.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\ntwin-bridge: WebSocket only\r\n")
        conn.close(); return
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


def main(argv=None):
    ap = argparse.ArgumentParser(description="Bridge the QEMU twin console to a WebSocket for the Studio.")
    ap.add_argument("--port", type=int, default=int(os.environ.get("BRIDGE_PORT", "8081")))
    ap.add_argument("--serial", default=os.environ.get("SERIAL_SOCK", "/tmp/plc-serial"),
                    help="twin console: a unix socket path, or host:port for TCP")
    a = ap.parse_args(argv)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", a.port)); srv.listen(4)
    print("twin-bridge: ws://localhost:%d  ->  %s" % (a.port, a.serial), flush=True)
    print("In the Studio, click 'Connect twin' and use this URL (or the forwarded Codespace URL).", flush=True)
    try:
        while True:
            conn, _ = srv.accept()
            threading.Thread(target=handle, args=(conn, a.serial), daemon=True).start()
    except KeyboardInterrupt:
        print("\ntwin-bridge: stopped", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
