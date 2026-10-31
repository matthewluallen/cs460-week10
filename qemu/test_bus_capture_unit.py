#!/usr/bin/env python3
"""Unit test for the Studio server's /bus/capture logic — runs with NO twin.

A small QMP test double (a unix socket that answers the handful of commands the capture
sends) writes trace lines in the released twin's exact esp32_i2c_observe format to whatever
file the bridge selects with `logfile`, only while the event is switched on. The test then checks
what the student-facing endpoint promises: tracing switched on and OFF again, the logfile parked
on /dev/null afterwards, IN1 drawn from a /field write, the 0x59 relay write found, the Studio
bundle accepted by the same rules the Studio importer applies, and the file-name allowlist.

This double exists only for this test; the lab path is the real twin (tools/test-live-tooling.sh).

    python3 qemu/test_bus_capture_unit.py      # exit 0 = pass
"""
import importlib.util, json, os, re, shutil, socket, sys, tempfile, threading, time, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
FMT = ("%d@%d.%06d:esp32_i2c_observe controller=0x55aa00112233 path=/machine/soc/i2c0 seq=%d epoch=0 "
       "virtual_ns=%d event=%s address=%d direction=%d data=%d nack=%d\n")


class QmpDouble(threading.Thread):
    def __init__(self, path):
        super().__init__(daemon=True)
        self.path = path; self.log = None; self.on = False; self.di = 0x04; self.relays = 0x08
        self.cmds = []; self.seq = 0; self.ns = 0
        self.srv = socket.socket(socket.AF_UNIX); self.srv.bind(path); self.srv.listen(4)
        threading.Thread(target=self._emit, daemon=True).start()

    def _emit(self):                               # one 50 ms "scan" while tracing is on
        while True:
            time.sleep(0.05)
            if not (self.on and self.log and self.log != "/dev/null"):
                continue
            relay = 0x03 if self.di & 1 else self.relays
            rows = [("address", 0x59, 0, 0xB2, 0), ("write", 0x59, 0, 0x00, 0), ("rstart_command", -1, -1, -1, -1),
                    ("address", 0x59, 1, 0xB3, 0), ("read", 0x59, 1, 0x40 | ((self.di & 0xF) << 4), -1),
                    ("stop", -1, -1, -1, -1), ("address", 0x59, 0, 0xB2, 0), ("write", 0x59, 0, 0x02, 0),
                    ("write", 0x59, 0, relay, 0), ("stop", -1, -1, -1, -1)]
            with open(self.log, "a") as f:
                for ev, a, d, data, nack in rows:
                    self.seq += 1; self.ns += 20000; w = time.time()
                    f.write(FMT % (99, int(w), int((w % 1) * 1e6), self.seq, self.ns, ev, a, d, data, nack))
            self.ns += 50_000_000

    def run(self):
        while True:
            c, _ = self.srv.accept()
            f = c.makefile("rw")
            f.write('{"QMP": {}}\n'); f.flush()
            for line in f:
                m = json.loads(line); ex = m["execute"]; a = m.get("arguments", {}); self.cmds.append((ex, a))
                if ex == "trace-event-get-state":
                    r = {"return": [{"name": a["name"], "state": "disabled"}]} if a["name"] == "esp32_i2c_observe" \
                        else {"error": {"desc": "No such trace event"}}
                elif ex == "trace-event-set-state":
                    self.on = a["enable"]; r = {"return": {}}
                elif ex == "human-monitor-command":
                    cl = a["command-line"]; assert cl.startswith("logfile "), cl
                    self.log = cl.split(" ", 1)[1]
                    if self.log != "/dev/null":
                        open(self.log, "w").close()
                    r = {"return": ""}
                elif ex == "qom-get":
                    r = {"return": self.di if a["property"] == "di" else self.relays}
                elif ex == "qom-set":
                    self.di = a["value"]; r = {"return": {}}
                else:
                    r = {"return": {}}
                f.write(json.dumps(r) + "\n"); f.flush()
            c.close()


def load_bridge():
    spec = importlib.util.spec_from_file_location("twin_bridge", os.path.join(HERE, "twin-bridge.py"))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m


class BusCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="buscap-")
        cls.qmp = os.path.join(cls.tmp, "qmp.sock")
        cls.double = QmpDouble(cls.qmp); cls.double.start()
        cls.B = load_bridge()
        cls.B.QMP_SOCK = cls.qmp
        cls.B.BUS_OUT_DIR = os.path.join(cls.tmp, "out")
        cls.B.BUS_TRACE_FILE = os.path.join(cls.tmp, "trace.log")
        cls.B._twin_identity = lambda with_hash=False: {"ok": True, "pid": 1, "profile": "vuln",
                                                        "flash_sha256": "a" * 64, "qemu_sha256": "b" * 64,
                                                        "app": {"elf_sha256": "c" * 64}}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_capture_end_to_end(self):
        B = self.B

        def press():                               # the student presses IN1 mid-capture
            time.sleep(0.6)
            B._qmp([("qom-set", {"path": B._FIELD_PATH, "property": "di", "value": 0x05})])
            B._bus_note_field(0x05)
        threading.Thread(target=press).start()
        res, code = B._bus_capture({"seconds": ["2"]})
        self.assertEqual(code, 200, res)
        self.assertTrue(res["ok"], res.get("error"))
        self.assertFalse(self.double.on, "tracing left ON")
        self.assertEqual(self.double.log, "/dev/null", "logfile not parked")
        self.assertFalse(os.path.exists(B.BUS_TRACE_FILE), "raw trace not cleaned up")
        vals = [c["value"] for c in res["summary"]["relay_write_changes"]]
        self.assertEqual(vals[0], "0x08"); self.assertIn("0x03", vals)
        self.assertIn("IN1", res["summary"]["channels"])
        self.assertEqual(len(res["field"]["writes"]), 1)
        for k in ("vcd", "json", "trace", "manifest", "studio"):
            self.assertIn(k, res["files"])
            self.assertTrue(B._FILE_RE.match(res["files"][k]), res["files"][k])
            self.assertTrue(os.path.isfile(os.path.join(B.BUS_OUT_DIR, res["files"][k])))
        if shutil.which("sigrok-cli"):
            self.assertTrue(res["sigrok"]["ok"], res["sigrok"])
            self.assertIn("write: 59", res["sigrok"]["addresses"])
            self.assertIn("0x03", [c["value"] for c in res["sigrok"]["relay_write_changes_0x59_reg02"]])
        # the Studio importer's rules (logic_studio.html parseQemuBusCapture)
        b = json.load(open(os.path.join(B.BUS_OUT_DIR, res["files"]["studio"])))
        rx = re.compile(r"^esp32_i2c_observe controller=(0x[0-9a-f]+) path=(/machine/[a-zA-Z0-9_./-]+) seq=(\d+) "
                        r"epoch=(\d+) virtual_ns=(\d+) event=(\w+) address=(-?\d+) direction=(-?\d+) data=(-?\d+) nack=(-?\d+)$")
        rows = [ln for ln in b["trace"].split("\n") if ln]
        self.assertTrue(all(rx.match(ln) for ln in rows))
        self.assertEqual(b["manifest"]["records"], len(rows))
        self.assertRegex(b["manifest"]["session"], r"^[0-9a-f-]{36}$")
        import hashlib
        self.assertEqual(hashlib.sha256(b["trace"].encode()).hexdigest(), b["manifest"]["files"]["trace.log"])
        seqs = [int(rx.match(ln).group(3)) for ln in rows]
        self.assertEqual(seqs, list(range(seqs[0], seqs[0] + len(seqs))), "sequence gap")
        listing = B._bus_list()
        self.assertEqual(listing["captures"][0]["id"], res["id"])

    def test_busy_and_bad_args(self):
        B = self.B
        self.assertEqual(B._bus_capture({"seconds": ["x"]})[1], 400)
        self.assertEqual(B._bus_capture({"bus": ["spi0"]})[1], 400)
        B._bus_lock.acquire()
        try:
            self.assertEqual(B._bus_capture({"seconds": ["1"]})[1], 409)
        finally:
            B._bus_lock.release()

    def test_file_allowlist_and_shadow(self):
        B = self.B
        for bad in ("../x.vcd", "bus-20260101-000000-i2c0.vcd/../../etc", "x.json", "bus-1-i2c0.vcd"):
            self.assertFalse(B._FILE_RE.match(bad), bad)
        B._BRIDGE_ADDR.update(bind="127.0.0.1", port=8080)
        self.assertTrue(B._shadowed("127.0.0.1:8080"))
        self.assertFalse(B._shadowed("127.0.0.1:8088"))
        B._BRIDGE_ADDR.update(bind="127.0.0.1", port=8088)
        self.assertFalse(B._shadowed("127.0.0.1:8080"))
        B._BRIDGE_ADDR.update(bind="0.0.0.0", port=8080)
        self.assertTrue(B._shadowed("127.0.0.1:8080"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
