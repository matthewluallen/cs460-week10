#!/usr/bin/env python3
"""Unit test for the virtual bus tap converter (no twin needed).

Builds trace lines in the exact format string the released twin binary prints for its
esp32_i2c_observe event, converts them, and checks that sigrok-cli's own I2C decoder reads back
the same bytes. These lines are a converter test input, generated here and deleted afterwards;
they are not a lab capture and nothing in the lab loads them.

    python3 bus-analysis/tools/test_trace_to_sigrok.py      # exit 0 = pass
"""
import json, os, shutil, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import trace_to_sigrok as T  # noqa: E402

FMT = ("%d@%d.%06d:esp32_i2c_observe controller=0x5555deadbeef path=/machine/soc/%s seq=%d epoch=0 "
       "virtual_ns=%d event=%s address=%d direction=%d data=%d nack=%d")


class Gen:
    def __init__(self):
        self.seq = 0; self.lines = []; self.wall = 1700000000.0

    def ev(self, ns, event, addr=-1, d=-1, data=-1, nack=-1, bus="i2c0"):
        self.seq += 1
        w = self.wall + ns / 1e9
        self.lines.append(FMT % (4242, int(w), int((w % 1) * 1e6), bus, self.seq, ns, event, addr, d, data, nack))

    def write(self, ns, addr, payload, bus="i2c0"):
        self.ev(ns, "address", addr, 0, addr << 1, 0, bus)
        for i, b in enumerate(payload):
            self.ev(ns + 1000 * (i + 1), "write", addr, 0, b, 0, bus)
        self.ev(ns + 1000 * (len(payload) + 1), "stop", bus=bus)

    def read_reg(self, ns, addr, reg, value):
        self.ev(ns, "address", addr, 0, addr << 1, 0)
        self.ev(ns + 1000, "write", addr, 0, reg, 0)
        self.ev(ns + 1500, "rstart_command")
        self.ev(ns + 2000, "address", addr, 1, (addr << 1) | 1, 0)
        self.ev(ns + 3000, "read", addr, 1, value, -1)
        self.ev(ns + 4000, "stop")


def scan_trace():
    g = Gen()
    relay = 0x08
    for k in range(6):                                   # six 50 ms scans
        t = k * 50_000_000
        di = 0x40 if k < 2 else 0x50                     # IN1 pressed from scan 2
        if k >= 2:
            relay = 0x03
        g.read_reg(t, 0x59, 0x00, di)
        g.write(t + 200_000, 0x59, [0x02, relay])
        g.read_reg(t + 400_000, 0x43, 0x0F, 0xFF)
        g.ev(t + 500_000, "tx_wait")
    g.ev(310_000_000, "peripheral-reset")
    g.write(320_000_000, 0x77, [0xD0], "i2c1")          # other bus, must be filtered out
    return g.lines


class TraceToSigrok(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="t2s-")

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def test_frames_and_summary(self):
        lines = scan_trace()
        field = {"di_start": 0x04, "writes": [{"t": 1700000000.0 + 0.09, "di": 0x05}]}
        summ, txns = T.convert(lines, "i2c0", os.path.join(self.d, "bus"), "virtual", field)
        self.assertEqual(summ["by_address"].get("0x59 W"), 12)          # 6 pointer writes + 6 relay writes
        self.assertEqual(summ["by_address"].get("0x59 R"), 6)
        self.assertNotIn("0x77 W", summ["by_address"])                 # i2c1 filtered out
        vals = [c["value"] for c in summ["relay_write_changes"]]
        self.assertEqual(vals, ["0x08", "0x03"])
        self.assertIn("RELAY 1 (relay0)", summ["relay_write_changes"][1]["on"])
        self.assertEqual([c["value"] for c in summ["input_read_changes_0x59_reg00"]], ["0x40", "0x50"])
        self.assertTrue(any(t["repeated_start"] for t in txns))
        self.assertEqual(summ["channels"], ["SCL", "SDA", "IN1", "IN2", "IN3", "IN4"])
        self.assertGreater(summ["drawing"]["span_s"], 0.25)            # virtual spacing kept

    def test_sigrok_decodes_same_bytes(self):
        if not shutil.which("sigrok-cli"):
            self.skipTest("sigrok-cli not installed")
        out = os.path.join(self.d, "bus")
        T.convert(scan_trace(), "i2c0", out, "virtual")
        dec = subprocess.run(["sigrok-cli", "-i", out + ".vcd", "-P", "i2c:scl=SCL:sda=SDA", "-A", "i2c"],
                             capture_output=True, text=True, timeout=120).stdout
        self.assertIn("Address write: 59", dec)
        self.assertIn("Address read: 59", dec)
        self.assertIn("Data write: 03", dec)
        self.assertIn("Data read: 50", dec)
        self.assertIn("Start repeat", dec)
        sr = out + ".sr"
        r = subprocess.run(["sigrok-cli", "-I", "vcd", "-i", out + ".vcd", "-o", sr],
                           capture_output=True, text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stderr)
        dec2 = subprocess.run(["sigrok-cli", "-i", sr, "-P", "i2c:scl=SCL:sda=SDA", "-A", "i2c"],
                              capture_output=True, text=True, timeout=120).stdout
        self.assertEqual(dec.count("Data write: 03"), dec2.count("Data write: 03"))

    def test_packed_compat_and_empty(self):
        evs, _ = T.parse_lines(scan_trace(), "i2c0")
        w, txns = T.build(evs)
        self.assertTrue(all(set(("addr", "dir", "bytes", "nack")) <= set(t) for t in txns))
        summ, txns = T.convert([], "i2c0", os.path.join(self.d, "empty"), "virtual")
        self.assertEqual(txns, [])
        self.assertTrue(summ["warnings"])

    def test_generic_format(self):
        lines = ["1@100.000001:i2c_event start(addr:0x59)", "1@100.000002:i2c_send send(addr:0x59) data:0x02",
                 "1@100.000003:i2c_send send(addr:0x59) data:0x01", "1@100.000004:i2c_event finish(addr:0x59)"]
        summ, txns = T.convert(lines, "i2c0", os.path.join(self.d, "gen"), "virtual")
        self.assertEqual(txns[0]["bytes"], [0x02, 0x01])
        self.assertEqual(summ["relay_write_changes"][0]["value"], "0x01")


if __name__ == "__main__":
    unittest.main(verbosity=2)
