#!/usr/bin/env python3
"""fw_identity.py — which firmware is in this flash image, read from the image itself.

Reads an ESP32-S3 flash image (the twin's 8 MB -drive file, or a dump taken from a board with
`esptool read_flash`) the way the ROM bootloader would:

  1. the partition table at 0x8000            -> where the app slots (ota_0, ota_1, factory) are
  2. the otadata partition                    -> which slot the bootloader selects
  3. the app image header + esp_app_desc_t    -> project, version, ESP-IDF version, build date,
                                                 and the app's ELF SHA-256 (the same value the
                                                 running firmware prints in `diag json`)

Nothing here runs or changes the device; it only reads bytes. Standard library only.

    python3 qemu/fw_identity.py qemu/twin/stamplc-flash-8MB.bin          # human summary
    python3 qemu/fw_identity.py --json qemu/twin/stamplc-flash-8MB.bin   # JSON
    python3 qemu/fw_identity.py --extract-app out/app.bin <image>       # copy the selected app slot

Honest limit: otadata names the slot the bootloader will pick at the NEXT boot. After an OTA
write and before the reboot, the code still executing is the previous slot; compare with the
firmware's own `diag json` elf_sha256 (a self-report) to tell the two apart.
"""
import hashlib, json, os, re, struct, sys, zlib

PT_OFFSET = 0x8000
APP_DESC_MAGIC = 0xABCD5432
IMAGE_MAGIC = 0xE9
SUBTYPE_NAMES = {0x00: "factory", 0x20: "test"}
SUBTYPE_NAMES.update({0x10 + i: "ota_%d" % i for i in range(16)})


def _cstr(b):
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def partitions(f):
    f.seek(PT_OFFSET)
    tbl = f.read(0xC00)
    out = []
    for i in range(0, len(tbl), 32):
        e = tbl[i:i + 32]
        if e[:2] != b"\xaa\x50":
            break
        ptype, sub, off, size = struct.unpack("<BBII", e[2:12])
        out.append({"type": ptype, "subtype": sub, "offset": off, "size": size,
                    "label": _cstr(e[12:28]),
                    "kind": ("app/" + SUBTYPE_NAMES.get(sub, hex(sub))) if ptype == 0 else "data/%d" % sub})
    return out


def _otadata_entries(f, part):
    out = []
    for sector in (0, 0x1000):
        f.seek(part["offset"] + sector)
        raw = f.read(32)
        if len(raw) < 32:
            continue
        seq, label, state, crc = struct.unpack("<I20sII", raw)
        if seq in (0, 0xFFFFFFFF):
            continue
        seqb = struct.pack("<I", seq)
        crc_ok = crc in (zlib.crc32(seqb, 0xFFFFFFFF) & 0xFFFFFFFF, zlib.crc32(seqb) & 0xFFFFFFFF)
        out.append({"seq": seq, "state": state, "crc_ok": crc_ok})
    return out


def selected_app(parts, f):
    """Return (partition, how) for the slot the bootloader selects."""
    apps = [p for p in parts if p["type"] == 0]
    otas = sorted([p for p in apps if 0x10 <= p["subtype"] < 0x20], key=lambda p: p["subtype"])
    factory = next((p for p in apps if p["subtype"] == 0x00), None)
    od = next((p for p in parts if p["type"] == 1 and p["subtype"] == 0x00), None)
    if od and otas:
        ents = [e for e in _otadata_entries(f, od) if e["crc_ok"] and e["state"] not in (3, 4)]
        if ents:
            best = max(ents, key=lambda e: e["seq"])
            return otas[(best["seq"] - 1) % len(otas)], "otadata seq %d" % best["seq"]
    if factory:
        return factory, "no valid otadata -> factory"
    if otas:
        return otas[0], "no valid otadata -> ota_0"
    return None, "no app partition"


def app_desc(f, offset):
    f.seek(offset)
    hdr = f.read(0x20 + 256)
    if len(hdr) < 0x20 + 256 or hdr[0] != IMAGE_MAGIC:
        return None
    d = hdr[0x20:]
    magic, secure_ver = struct.unpack("<II", d[:8])
    if magic != APP_DESC_MAGIC:
        return None
    return {"secure_version": secure_ver,
            "version": _cstr(d[16:48]), "project": _cstr(d[48:80]),
            "time": _cstr(d[80:96]), "date": _cstr(d[96:112]),
            "idf_version": _cstr(d[112:144]), "elf_sha256": d[144:176].hex()}


def app_image_length(f, offset, limit):
    """Length of the ESP app image at offset (header + segments + checksum [+ SHA-256])."""
    f.seek(offset)
    h = f.read(24)
    if len(h) < 24 or h[0] != IMAGE_MAGIC:
        return None
    nseg = h[1]
    hash_appended = h[23] == 1
    pos = offset + 24
    for _ in range(nseg):
        f.seek(pos)
        sh = f.read(8)
        if len(sh) < 8:
            return None
        _, ln = struct.unpack("<II", sh)
        pos += 8 + ln
        if pos - offset > limit:
            return None
    pos += 1                                  # checksum byte
    pos = (pos + 15) & ~15                    # padded to 16
    if hash_appended:
        pos += 32
    return pos - offset


def profile_of(path):
    base = os.path.basename(path or "")
    return {"stamplc-flash-8MB.bin": "vuln", "stamplc-vuln-8MB.bin": "vuln",
            "stamplc-secure-8MB.bin": "secure",
            "stamplc-flash-8MB-MALICIOUS.bin": "malicious"}.get(base, "custom")


def sha256_file(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(chunk), b""):
            h.update(b)
    return h.hexdigest()


def identify(path, with_hash=True):
    st = os.stat(path)
    with open(path, "rb") as f:
        parts = partitions(f)
        sel, how = selected_app(parts, f)
        slots = []
        for p in parts:
            if p["type"] == 0:
                slots.append(dict(p, app=app_desc(f, p["offset"])))
        out = {"image": os.path.abspath(path), "image_name": os.path.basename(path),
               "profile": profile_of(path), "size": st.st_size, "mtime": int(st.st_mtime),
               "partitions": parts, "app_slots": slots,
               "selected_slot": sel["label"] if sel else None, "selected_how": how,
               "app": app_desc(f, sel["offset"]) if sel else None,
               "note": "selected_slot is what the bootloader picks at the NEXT boot; after an OTA "
                       "write and before a reboot the running code is still the previous slot. "
                       "Compare app.elf_sha256 with the firmware's own `diag json` elf_sha256."}
        if sel:
            n = app_image_length(f, sel["offset"], sel["size"])
            out["app_length"] = n
            if n:
                f.seek(sel["offset"])
                out["app_sha256"] = hashlib.sha256(f.read(n)).hexdigest()
    if with_hash:
        out["flash_sha256"] = sha256_file(path)
    return out


def extract_app(path, dest):
    with open(path, "rb") as f:
        parts = partitions(f)
        sel, _ = selected_app(parts, f)
        if not sel:
            raise SystemExit("no app partition in %s" % path)
        n = app_image_length(f, sel["offset"], sel["size"]) or sel["size"]
        f.seek(sel["offset"])
        data = f.read(n)
    os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
    with open(dest, "wb") as o:
        o.write(data)
    return sel["label"], len(data)


def main(argv=None):
    a = list(sys.argv[1:] if argv is None else argv)
    as_json = "--json" in a
    a = [x for x in a if x != "--json"]
    if len(a) >= 3 and a[0] == "--extract-app":
        label, n = extract_app(a[2], a[1])
        print("extracted %s (%d bytes) -> %s" % (label, n, a[1]))
        return 0
    if len(a) != 1 or not os.path.isfile(a[0]):
        print(__doc__)
        return 2
    info = identify(a[0])
    if as_json:
        print(json.dumps(info, indent=1))
        return 0
    app = info["app"] or {}
    print("image      : %s  (%s profile, %d bytes)" % (info["image_name"], info["profile"], info["size"]))
    print("flash sha  : %s" % info["flash_sha256"])
    print("boot slot  : %s  (%s)" % (info["selected_slot"], info["selected_how"]))
    print("app        : project=%s version=%s built %s %s" % (app.get("project"), app.get("version"),
                                                              app.get("date"), app.get("time")))
    print("ESP-IDF    : %s" % app.get("idf_version"))
    print("ELF sha256 : %s   (compare: plc> diag json -> elf_sha256)" % app.get("elf_sha256"))
    if info.get("app_sha256"):
        print("app sha256 : %s  (%d bytes of the selected slot)" % (info["app_sha256"], info["app_length"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
