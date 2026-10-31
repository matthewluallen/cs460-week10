#!/usr/bin/env python3
"""mqtt/test-phone-points.py — end-to-end check of the network-only phone points.

Needs a RUNNING twin (vulnerable or secure) whose console nobody else holds (close
./qemu/console.sh and the Studio first) and the phone bridge's broker:

    (cd mqtt && npm install --no-audit --no-fund && node server.js) &   # MQTT/TCP :1883
    ./qemu/run.sh                                                        # in another shell
    python3 mqtt/test-phone-points.py

It plays the phone with a tiny raw-socket MQTT 3.1.1 client (no libraries) and asserts:
  1. every phone topic lands in its own point (`mqtt status`), null geo speed stays missing
  2. phone buttons never touch IN1-IN8: `io` shows DI6/DI7 = 0 while btn1/btn2 = 1
  3. the Modbus mirror (holding registers 140-165) carries the values and fresh bits
  4. `mqtt pin` refuses other ids (counted) but accepts anyone who reuses the pinned id
  5. each point goes stale on its own 8 s timer: analog holds, a momentary button reads 0
  6. a project that binds phone_light_lux + phone_light_lux_fresh drives a relay, and the
     fresh gate drops it when the phone goes quiet; then the factory program is reloaded
  7. the client lifecycle: `mqtt connect` again replaces the client cleanly (no heap loss over
     repeated reconnects), `mqtt disconnect` stops it and forgets the saved broker, and
     `mqtt connect` brings it back
Exit 0 = all passed. Env: SERIAL_SOCK, BROKER (host:port the script publishes to),
TWIN_BROKER (the host the twin dials; the twin maps 127.0.0.1 to this machine), MODBUS.
"""
import base64, json, os, re, select, socket, struct, subprocess, sys, tempfile, time, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERIAL = os.environ.get("SERIAL_SOCK", "/tmp/plc-serial")
BROKER = os.environ.get("BROKER", "127.0.0.1:1883")
TWIN_BROKER = os.environ.get("TWIN_BROKER", "127.0.0.1")
MODBUS = os.environ.get("MODBUS", "127.0.0.1:502")
results = []

def check(name, ok, detail=""):
    results.append(bool(ok))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail else ""), flush=True)

# ---- plc> console over the twin's unix socket ----
con = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
def drain(dur):
    out = b""; end = time.time() + dur
    while time.time() < end:
        r, _, _ = select.select([con], [], [], 0.2)
        if r:
            d = con.recv(65536)
            if d: out += d
    return out.decode("utf-8", "replace")
def cmd(line, wait=1.2):
    con.sendall(line.encode() + b"\n"); return drain(wait)
def status():
    for _ in range(3):
        m = re.search(r'\{"mqtt":"status".*\}\}', cmd("mqtt status", 2.5))
        if m: return json.loads(m.group(0))
    raise RuntimeError("no mqtt status JSON")
def relay(txt, n):
    m = re.search(rf"relay{n}\s+cmd=\d+\s+act=(\d)", txt); return m.group(1) if m else "?"

# ---- raw MQTT 3.1.1 publisher ----
def _len(n):
    out = b""
    while True:
        d, n = n % 128, n // 128
        out += bytes([d | 0x80 if n else d])
        if not n: return out
def _str(s):
    b = s.encode(); return struct.pack(">H", len(b)) + b
class Mqtt:
    def __init__(self, hostport, client_id):
        h, p = hostport.rsplit(":", 1)
        self.s = socket.create_connection((h, int(p)), timeout=5)
        body = _str("MQTT") + b"\x04\x02" + struct.pack(">H", 60) + _str(client_id)
        self.s.sendall(b"\x10" + _len(len(body)) + body)
        ack = self.s.recv(4)
        if ack[:2] != b"\x20\x02" or ack[3] != 0: raise RuntimeError(f"CONNACK {ack!r}")
    def pub(self, topic, obj):
        body = _str(topic) + json.dumps(obj).encode()          # QoS 0: no packet id
        self.s.sendall(b"\x30" + _len(len(body)) + body)
        time.sleep(0.2)   # pace like the phone page: the controller queues only a few messages

# ---- Modbus/TCP FC3 ----
def read_regs(addr, count):
    h, p = MODBUS.rsplit(":", 1)
    s = socket.create_connection((h, int(p)), timeout=5)
    s.sendall(struct.pack(">HHHBBHH", 1, 0, 6, 1, 3, addr, count))
    data = b""
    while len(data) < 9 + 2 * count:
        d = s.recv(512)
        if not d: break
        data += d
    s.close()
    return list(struct.unpack(f">{count}H", data[9:9 + 2 * count]))

def plcload(payload):
    frame = "PLCLOAD %08x %s" % (zlib.crc32(payload) & 0xFFFFFFFF, base64.b64encode(payload).decode())
    cmd("mode prog", 1.5)
    return cmd(frame, 4.0)

def main():
    con.connect(SERIAL); con.setblocking(False); drain(1.0)
    print("== phone points: twin", SERIAL, "· broker", BROKER, "==", flush=True)
    cmd("mqtt unpin", 1.0)
    def connect(wait=40):
        reply = cmd(f"mqtt connect {TWIN_BROKER}", 2.0).strip()
        print("  " + (reply.splitlines() or ["(no reply)"])[0][:200])
        t0 = time.time(); st = {}
        while time.time() - t0 < wait:
            st = status()
            if st.get("connected"): break
            time.sleep(1)
        return st
    st = connect()
    check("mqtt.connected", st.get("connected") is True,
          f"client={st.get('client')} broker={st.get('broker')} saved={st.get('saved_broker')} heap_free={st.get('heap_free')}")
    check("mqtt.authentication_none", st.get("authentication") == "none")

    ph = Mqtt(BROKER, "test-phone-points")
    me = "ph-test1"
    def phone(topic, obj, who=me): ph.pub(f"phone/{who}/{topic}", obj)
    phone("light", {"lux": 312.6}); phone("accel", {"x": 0.1, "y": 0.2, "z": 9.8, "mag": 9.81})
    phone("gyro", {"alpha": 12.4, "beta": -3, "gamma": 0.4})
    phone("orient", {"alpha": 271.2, "beta": -12.4, "gamma": 3})
    phone("sound", {"level": 42})
    phone("geo", {"lat": 41.879, "lon": -87.624, "acc": 20.0, "alt": None, "spd": None, "hdg": None})
    phone("battery", {"level": 87, "charging": True})
    phone("btn1", {"v": 1}); phone("btn2", {"v": 1})
    time.sleep(2.0)
    st = status(); p = st["points"]
    check("ingest.light", p["light_lux"]["v"] == 313 and p["light_lux"]["fresh"] == 1, str(p["light_lux"]))
    check("ingest.accel_x100", p["accel_x100"]["v"] == 981, str(p["accel_x100"]))
    check("ingest.gyro", (p["gyro_a"]["v"], p["gyro_b"]["v"], p["gyro_g"]["v"]) == (12, -3, 0))
    check("ingest.orient", (p["heading"]["v"], p["tilt_b"]["v"], p["tilt_g"]["v"]) == (271, -12, 3))
    check("ingest.sound", p["sound"]["v"] == 42)
    check("ingest.geo", p["lat_x1000"]["v"] == 41879 and p["lon_x1000"]["v"] == -87624 and
          p["speed_x100"]["fresh"] == 0 and p["speed_x100"]["age_ms"] is None, "null speed stays missing")
    check("ingest.battery", p["battery"]["v"] == 87 and p["charging"]["v"] == 1)
    check("ingest.buttons", p["btn1"]["v"] == 1 and p["btn2"]["v"] == 1)
    di = re.search(r"DI\s*:\s*(.*)", cmd("io", 1.5))
    bits = (di.group(1).split() if di else [])
    check("buttons.not_IN7_IN8", len(bits) == 8 and bits[6].rstrip("F") == "0" and bits[7].rstrip("F") == "0",
          f"DI line: {' '.join(bits)}")
    regs = read_regs(140, 26)
    fresh = regs[19] | regs[20] << 16
    check("modbus.mirror", regs[0] == 313 and regs[17] == 1 and regs[18] == 1 and (regs[11] << 16 | regs[12]) == 41879
          and fresh & 1 and fresh & (1 << 15) and not fresh & (1 << 9), f"regs 140..165 = {regs}")

    # pinning: identity by claim
    print("  " + cmd(f"mqtt pin {me}", 1.2).strip().splitlines()[0][:160])
    phone("light", {"lux": 60000}, who="ph-evil")
    time.sleep(1.5); st = status()
    check("pin.other_id_refused", st["rejected"]["other_id"] >= 1 and st["points"]["light_lux"]["v"] != 60000,
          f"rejected={st['rejected']} light={st['points']['light_lux']}")
    phone("light", {"lux": 60000})                      # anyone can CLAIM the pinned id
    time.sleep(1.5); st = status()
    check("pin.claim_not_proof", st["points"]["light_lux"]["v"] == 60000 and st["pinned"] == me,
          "a spoofed value under the pinned id is accepted and fresh")
    cmd("mqtt unpin", 1.0)

    # freshness: each point has its own 8 s timer
    phone("light", {"lux": 640}); phone("btn1", {"v": 1})
    time.sleep(3.0); phone("sound", {"level": 55})
    time.sleep(6.5)                                      # light/btn1 are now ~9.5 s old, sound ~6.5 s
    p = status()["points"]
    check("stale.analog_holds", p["light_lux"]["fresh"] == 0 and p["light_lux"]["v"] == 640, str(p["light_lux"]))
    check("stale.button_releases", p["btn1"]["fresh"] == 0 and p["btn1"]["v"] == 0, str(p["btn1"]))
    check("stale.per_point", p["sound"]["fresh"] == 1, str(p["sound"]))

    # a project that reads phone points through explicit word sources
    proj = {"schema": "tbd.logic-project/v1", "id": "phone-points-test", "name": "Phone light alarm (test)", "revision": 1,
            "program": {"scan_ms": 50, "nets": [
                {"id": "N1", "label": "light > 500 and fresh", "expr": {"t": "series", "id": "e1", "kids": [
                    {"t": "cmp", "id": "e2", "op": "GT", "idx": 0, "k": 500},
                    {"t": "cmp", "id": "e3", "op": "EQ", "idx": 1, "k": 1}]}, "coil": {"io": "QX", "idx": 0, "neg": False}},
                {"id": "N2", "label": "phone button 1", "expr": {"t": "cmp", "id": "e4", "op": "EQ", "idx": 2, "k": 1},
                 "coil": {"io": "QX", "idx": 1, "neg": False}}]},
            "symbols": {"IW0": {"name": "phone_lux"}, "IW1": {"name": "phone_lux_ok"}, "IW2": {"name": "phone_b1"},
                        "QX0": {"name": "light_alarm"}, "QX1": {"name": "b1_lamp"}},
            "settings": {"bindings": {"digital_inputs": [0, 1, 2, 3, 4, 5, 6, 7], "digital_outputs": [0, 1, 2, 3],
                                      "word_inputs": ["phone_light_lux", "phone_light_lux_fresh", "phone_btn1"] + ["parameter"] * 5},
                         "parameters": [0] * 8}}
    with tempfile.NamedTemporaryFile("w", suffix=".csco", delete=False) as f:
        json.dump(proj, f); path = f.name
    out = subprocess.run(["node", os.path.join(ROOT, "tools", "compile-project.mjs"), path],
                         capture_output=True, text=True).stdout.strip().splitlines()
    os.unlink(path)
    load = plcload(out[-1].encode())
    check("project.phone_sources_load", "[LOADER] OK" in load, (load.strip().splitlines() or ["?"])[0][:120])
    cmd("mode run", 2.0)
    phone("light", {"lux": 800}); phone("btn1", {"v": 1})
    time.sleep(2.0); io1 = cmd("io", 1.5)
    check("project.phone_drives_relays", relay(io1, 0) == "1" and relay(io1, 1) == "1",
          f"relay0={relay(io1, 0)} relay1={relay(io1, 1)}")
    time.sleep(9.0); io2 = cmd("io", 1.5)                # phone silent: fresh gate drops the alarm
    check("project.fresh_gate", relay(io2, 0) == "0" and relay(io2, 1) == "0",
          f"after 9 s of silence relay0={relay(io2, 0)} relay1={relay(io2, 1)}")

    # client lifecycle: reconnect replaces the client without losing heap; disconnect stops it
    heaps = []
    for _ in range(3):
        st = connect()
        time.sleep(2.0); heaps.append(status().get("heap_free", 0))
    check("lifecycle.reconnect", st.get("connected") is True and st.get("clients_started", 0) >= 4,
          f"clients_started={st.get('clients_started')}")
    check("lifecycle.no_heap_leak", heaps and max(heaps) - min(heaps) < 3072 and heaps[-1] >= heaps[0] - 3072,
          f"heap_free after each reconnect: {heaps}")
    reply = cmd("mqtt disconnect", 2.0)
    t0 = time.time(); st = {}
    while time.time() - t0 < 20:
        st = status()
        if st.get("client") == "idle": break
        time.sleep(1)
    check("lifecycle.disconnect", st.get("client") == "idle" and st.get("connected") is False and st.get("saved_broker") == ""
          and st.get("heap_free", 0) > heaps[-1], f"client={st.get('client')} saved={st.get('saved_broker')!r} heap_free={st.get('heap_free')}")
    st = connect()
    check("lifecycle.connect_again", st.get("connected") is True and st.get("saved_broker") == TWIN_BROKER,
          f"client={st.get('client')} saved={st.get('saved_broker')}")

    # restore the factory default program (PROGRAM mode, as shipped)
    factory = open(os.path.join(ROOT, "sbom", "firmware", "factory", "default-project.json"), "rb").read()
    back = plcload(factory)
    pj = cmd("project json", 3.0)
    check("restore.factory_program", "[LOADER] OK" in back and "motor-start-stop-estop" in pj)
    n = sum(results)
    print(f"\n== {n}/{len(results)} phone-point checks passed ==", flush=True)
    return 0 if n == len(results) else 1

if __name__ == "__main__":
    sys.exit(main())
