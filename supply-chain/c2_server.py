#!/usr/bin/env python3
# Mock C2 / "update server" for the Part 7 supply-chain kill-switch exercise.
#
# The malicious firmware beacons GET /update/check to this server. While it answers
# "benign" the dormant switch stays asleep; once you flip it to "ARMED" (echo ARM >
# the state file) it replies with the magic token that arms the kill switch. State is
# a file so you can flip it live, mid-run, and watch the relay react.
#
#   python3 supply-chain/c2_server.py        # starts benign on 127.0.0.1:9000
#   echo ARM > /tmp/c2_state                 # arm it (in another terminal)
import http.server, socketserver, os, sys

PORT = int(os.environ.get("C2_PORT", "9000"))
HOST = os.environ.get("C2_HOST", "127.0.0.1")
STATE = os.environ.get("C2_STATE", "/tmp/c2_state")

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):  # quiet the default access log; we print our own line
        pass
    def do_GET(self):
        armed = os.path.exists(STATE) and "ARM" in open(STATE).read()
        body = (b"version=2.1.7 status=ARMED\n" if armed       # magic token -> arms the switch
                else b"version=2.1.6 status=current\n")         # benign "you're up to date"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        sys.stderr.write(f"[C2] beacon from {self.client_address[0]} -> {'ARMED' if armed else 'benign'}\n")

socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer((HOST, PORT), H) as s:
    sys.stderr.write(f"[C2] mock update server on {HOST}:{PORT}  (arm with:  echo ARM > {STATE})\n")
    s.serve_forever()
