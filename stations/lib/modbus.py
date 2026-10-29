"""Tiny dependency-free Modbus/TCP client (shared by the HMI and engineer stations).

Only the function codes the StampLC twin implements, enough for a SCADA master:
FC1 read coils, FC3 read holding registers, FC5 write single coil, FC6 write single
register. Point addresses come from sbom/firmware/point_map.json.
"""
import socket, struct, time

# --- the controller's point map (sbom/firmware/point_map.json) ---
COILS = {"estop": 0, "relay0": 1, "relay1": 2, "relay2": 3, "relay3": 4, "ssr1": 5, "ssr2": 6}
DI_MIRROR_BASE = 100            # coils 100..107 mirror DI0..7
REGS = {"vin_mv": 0, "current_ma": 1, "temp_c_x10": 2,
        "aio_ch1_mv": 10, "aio_ch1_ua": 11, "aio_ch2_mv": 12, "aio_ch2_ua": 13,
        "plc_mode": 20, "scan_last_us": 21, "heartbeat": 25, "setpoint_tempx10": 30}
MODES = ["PROGRAM", "RUN", "REMOTE"]


class Modbus:
    def __init__(self, host, port=502, unit=1, timeout=3.0):
        self.host, self.port, self.unit, self.timeout = host, port, unit, timeout
        self._tid = 0

    def _txn(self, pdu):
        self._tid = (self._tid + 1) & 0xFFFF
        adu = struct.pack(">HHHB", self._tid, 0, len(pdu) + 1, self.unit) + pdu
        s = socket.create_connection((self.host, self.port), timeout=self.timeout)
        try:
            s.sendall(adu)
            hdr = b""
            while len(hdr) < 6:
                c = s.recv(6 - len(hdr))
                if not c:
                    raise ConnectionError("short header")
                hdr += c
            ln = struct.unpack(">HHH", hdr)[2]
            body = b""
            while len(body) < ln:
                c = s.recv(ln - len(body))
                if not c:
                    raise ConnectionError("short body")
                body += c
        finally:
            s.close()
        return adu, hdr + body

    def read_regs(self, addr, count):
        _, resp = self._txn(struct.pack(">BHH", 3, addr, count))
        pdu = resp[7:]
        if pdu[0] & 0x80:
            raise IOError("modbus exception %d" % pdu[1])
        nb = pdu[1]
        return [struct.unpack(">H", pdu[2 + i*2:4 + i*2])[0] for i in range(nb // 2)]

    def read_coils(self, addr, count):
        _, resp = self._txn(struct.pack(">BHH", 1, addr, count))
        pdu = resp[7:]
        if pdu[0] & 0x80:
            raise IOError("modbus exception %d" % pdu[1])
        out = []
        for i in range(count):
            byte = pdu[2 + (i // 8)]
            out.append((byte >> (i % 8)) & 1)
        return out

    def write_coil(self, addr, on):
        req, resp = self._txn(struct.pack(">BHH", 5, addr, 0xFF00 if on else 0x0000))
        return req.hex(), resp.hex()

    def write_reg(self, addr, value):
        req, resp = self._txn(struct.pack(">BHH", 6, addr, value & 0xFFFF))
        return req.hex(), resp.hex()

    # convenience
    def process(self):
        r = self.read_regs(0, 26)
        return {"vin_v": r[0] / 1000.0, "current_ma": r[1], "temp_c": r[2] / 10.0,
                "mode": MODES[r[20]] if r[20] < 3 else r[20], "scan_ms": r[21] / 1000.0,
                "heartbeat": r[25]}

    def relays(self):
        c = self.read_coils(1, 4)
        return {"relay0": c[0], "relay1": c[1], "relay2": c[2], "relay3": c[3]}


def wait_for(host, port=502, tries=120, delay=1.0):
    """Block until the controller's Modbus port answers (the twin takes a bit to boot)."""
    for _ in range(tries):
        try:
            Modbus(host, port, timeout=1.5).read_regs(25, 1)
            return True
        except Exception:
            time.sleep(delay)
    return False
