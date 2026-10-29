#!/usr/bin/env python3
"""HMI station — a small SCADA that polls the PLC over Modbus/TCP and serves a live
dashboard. This is a SEPARATE node on the plant network: every value it shows arrived
as a real Modbus frame over the wire (capture it on the `capture` station).
"""
import json, os, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import sys
sys.path.insert(0, "/lib")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from modbus import Modbus, wait_for

PLC = os.environ.get("PLC_HOST", "plc")
PORT = int(os.environ.get("HMI_PORT", "8800"))
state = {"ok": False, "ts": 0, "process": {}, "relays": {}, "error": "connecting…"}

def poller():
    wait_for(PLC)
    mb = Modbus(PLC)
    while True:
        try:
            p = mb.process(); r = mb.relays()
            state.update(ok=True, ts=time.time(), process=p, relays=r, error="")
        except Exception as e:
            state.update(ok=False, error=str(e))
        time.sleep(1.0)

PAGE = """<!doctype html><meta charset=utf-8><title>Plant HMI</title>
<meta name=viewport content="width=device-width,initial-scale=1">
<style>
 body{font:14px/1.5 system-ui,sans-serif;background:#0d1017;color:#e7eef6;margin:0;padding:22px}
 h1{font-size:18px;margin:0 0 2px} .sub{color:#8498ad;font:12px ui-monospace,monospace;margin-bottom:18px}
 .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px;max-width:760px}
 .tile{background:#121a28;border:1px solid #1e2a38;border-radius:12px;padding:14px}
 .k{font:11px ui-monospace,monospace;color:#8498ad;text-transform:uppercase;letter-spacing:.08em}
 .v{font-size:26px;font-weight:700;margin-top:6px;color:#5ff0b0}
 .relays{display:flex;gap:10px;margin-top:16px;max-width:760px;flex-wrap:wrap}
 .r{display:flex;align-items:center;gap:8px;background:#121a28;border:1px solid #1e2a38;border-radius:10px;padding:9px 13px}
 .led{width:13px;height:13px;border-radius:50%;background:#2a3650} .led.on{background:#5ff0b0;box-shadow:0 0 10px #5ff0b0}
 .err{color:#ff7a7a;font:12px ui-monospace,monospace;margin-top:14px}
 .note{color:#8498ad;font-size:12px;margin-top:18px;max-width:760px}
</style>
<h1>Plant HMI <span style="color:#4d9fff">(SCADA station)</span></h1>
<div class=sub>polling the controller at <b id=plc></b> over Modbus/TCP :502 — every value is a real frame on the wire</div>
<div class=grid id=grid></div>
<div class=relays id=relays></div>
<div class=err id=err></div>
<div class=note>This is a separate station. The <b>capture</b> station sees these Modbus Read frames; the <b>engineer</b> station can write the controller over the same protocol (CWE-306 on the vulnerable build).</div>
<script>
async function tick(){
 const s=await fetch('/api/state').then(r=>r.json());
 document.getElementById('plc').textContent=s.plc;
 const p=s.process||{};
 document.getElementById('grid').innerHTML=[
  ['Supply voltage',(p.vin_v??0).toFixed(2)+' V'],['Output current',(p.current_ma??0)+' mA'],
  ['Temperature',(p.temp_c??0).toFixed(1)+' °C'],['Controller mode',p.mode??'—'],
  ['Scan time',(p.scan_ms??0).toFixed(1)+' ms'],['Heartbeat',p.heartbeat??'—']
 ].map(([k,v])=>`<div class=tile><div class=k>${k}</div><div class=v>${v}</div></div>`).join('');
 const r=s.relays||{};
 document.getElementById('relays').innerHTML=['relay0','relay1','relay2','relay3'].map(n=>
  `<div class=r><span class="led ${r[n]?'on':''}"></span>${n}</div>`).join('');
 document.getElementById('err').textContent=s.ok?'':('⚠ '+(s.error||'no data'));
}
setInterval(tick,1000);tick();
</script>"""

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path.startswith("/api/state"):
            body = json.dumps({**state, "plc": PLC + ":502"}).encode()
            self.send_response(200); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
        else:
            body = PAGE.encode()
            self.send_response(200); self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

if __name__ == "__main__":
    threading.Thread(target=poller, daemon=True).start()
    print("HMI station: http://0.0.0.0:%d  polling %s:502" % (PORT, PLC), flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
