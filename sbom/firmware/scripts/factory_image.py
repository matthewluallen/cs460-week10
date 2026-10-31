#!/usr/bin/env python3
"""Build a bootable StampLC flash image with the factory default project already installed.

How the controller stores its program: program_loader.cpp persists the installed project
as ONE NVS blob -- namespace "plc", key "project_v1" -- holding the exact TBD-PROJECT JSON
bytes the Studio sends with PLCLOAD. At boot it reads that blob, validates it and installs
it in PROGRAM mode (unless the project itself declares startup RUN; the default does not).

Before this script, the twin release image got its startup project by being USED: the
8 MB file had been booted in QEMU, a project was loaded from the Studio, QEMU wrote the
NVS pages back into the file, and that file was published. This script makes the same
end state reproducible from source: it writes the NVS partition directly and merges it
with the build outputs at the documented offsets.

    python3 scripts/factory_image.py nvs   --out nvs.bin
    python3 scripts/factory_image.py merge --build-dir .pio/build/m5stamplc --out image.bin [--pad-8mb]
    python3 scripts/factory_image.py check                       # payload sanity only

The NVS writer below covers exactly this case (one namespace, one blob that fits one
page, NVS format version 2). `--crosscheck` compares it byte-for-byte with Espressif's
esp-idf-nvs-partition-gen when that package is installed.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

FW = Path(__file__).resolve().parents[1]
DEFAULT_PROJECT = FW / 'factory' / 'default-project.json'
NVS_OFFSET, NVS_SIZE = 0x9000, 0x5000
PARTS = (('bootloader.bin', 0x0), ('partitions.bin', 0x8000), ('boot_app0.bin', 0xE000), ('firmware.bin', 0x10000))
FLASH_SIZE = 8 * 1024 * 1024
NAMESPACE, KEY = 'plc', 'project_v1'     # program_loader.cpp NVS_NS / NVS_PROG
PAGE, ENTRY, MAX_ENTRIES = 4096, 32, 126
PAYLOAD_CAPACITY = 4096                  # project_core.h PayloadCapacity


def crc(data):
    return zlib.crc32(bytes(data), 0xFFFFFFFF) & 0xFFFFFFFF


def project_payload(path=DEFAULT_PROJECT):
    """The exact bytes the Studio's deploymentText() produces; no trailing newline."""
    data = Path(path).read_bytes()
    if data != data.strip() or not data:
        sys.exit(f'{path}: must hold the compiled payload exactly (no surrounding whitespace)')
    if len(data) > PAYLOAD_CAPACITY:
        sys.exit(f'{path}: {len(data)} bytes exceeds the 4096-byte project capacity')
    doc = json.loads(data)
    if doc.get('magic') != 'TBD-PROJECT' or doc.get('schema') != 1:
        sys.exit(f'{path}: not a TBD-PROJECT schema 1 payload')
    if 'startup' in doc:
        sys.exit(f'{path}: the factory default must boot in PROGRAM (no startup declaration)')
    return data


def entry(ns, etype, span, chunk, key, data8):
    e = bytearray(b'\xff' * ENTRY)
    e[0], e[1], e[2], e[3] = ns, etype, span, chunk
    e[8:24] = key.encode().ljust(16, b'\x00')
    e[24:32] = data8
    struct.pack_into('<I', e, 4, crc(e[0:4] + e[8:32]))
    return e


def nvs_partition(blob, size=NVS_SIZE):
    """One active page: namespace entry, BLOB_DATA (+ raw data entries), BLOB_IDX."""
    if len(blob) > (MAX_ENTRIES - 3) * ENTRY:
        raise ValueError('blob does not fit one NVS page; extend the writer before growing the payload')
    page = bytearray(b'\xff' * PAGE)
    struct.pack_into('<II', page, 0, 0xFFFFFFFE, 0)        # state ACTIVE, sequence 0
    page[8] = 0xFE                                          # NVS format version 2
    struct.pack_into('<I', page, 28, crc(page[4:28]))
    entries = []
    entries.append(entry(0, 0x01, 1, 0xFF, NAMESPACE, bytes([1]) + b'\xff' * 7))   # U8 namespace "plc" -> 1
    data_entries = (len(blob) + ENTRY - 1) // ENTRY
    entries.append(entry(1, 0x42, data_entries + 1, 0, KEY,
                         struct.pack('<H', len(blob)) + b'\xff\xff' + struct.pack('<I', crc(blob))))
    raw = bytearray(blob) + b'\xff' * (data_entries * ENTRY - len(blob))
    entries += [raw[i:i + ENTRY] for i in range(0, len(raw), ENTRY)]
    entries.append(entry(1, 0x48, 1, 0xFF, KEY, struct.pack('<I', len(blob)) + bytes([1, 0]) + b'\xff\xff'))
    bitmap = bytearray(b'\xff' * 32)
    for n, e in enumerate(entries):
        page[64 + n * ENTRY:64 + (n + 1) * ENTRY] = e
        bitmap[(2 * n) // 8] &= ~(1 << ((2 * n) & 7)) & 0xFF   # 0b11 empty -> 0b10 written
    page[32:64] = bitmap
    return bytes(page) + b'\xff' * (size - PAGE)


def crosscheck(blob, ours):
    try:
        from esp_idf_nvs_partition_gen import nvs_partition_gen  # noqa: F401
    except ImportError:
        print('crosscheck skipped: pip install esp-idf-nvs-partition-gen to compare with the official generator')
        return None
    with tempfile.TemporaryDirectory() as d:
        Path(d, 'p.json').write_bytes(blob)
        Path(d, 'nvs.csv').write_text(f'key,type,encoding,value\n{NAMESPACE},namespace,,\n{KEY},file,binary,{d}/p.json\n')
        subprocess.run([sys.executable, '-m', 'esp_idf_nvs_partition_gen', 'generate', f'{d}/nvs.csv', f'{d}/o.bin', hex(NVS_SIZE)],
                       check=True, stdout=subprocess.DEVNULL)
        official = Path(d, 'o.bin').read_bytes()
    same = official == ours
    print('crosscheck vs esp-idf-nvs-partition-gen:', 'IDENTICAL' if same else 'DIFFERENT')
    if not same:
        sys.exit(1)
    return same


def merge(build_dir, nvs, pad):
    build_dir = Path(build_dir)
    image = bytearray()
    sources = []
    for name, offset in PARTS:
        path = build_dir / name
        if name == 'boot_app0.bin' and not path.exists():
            path = find_boot_app0()
        data = path.read_bytes()
        sources.append((offset, data))
    sources.append((NVS_OFFSET, nvs))
    for offset, data in sorted(sources):
        if len(image) > offset:
            sys.exit(f'overlap at {offset:#x}')
        image += b'\xff' * (offset - len(image)) + data
    if pad:
        if len(image) > FLASH_SIZE:
            sys.exit('image larger than 8 MB')
        image += b'\xff' * (FLASH_SIZE - len(image))
    return bytes(image)


def find_boot_app0():
    for root in (FW / '.pio', Path.home() / '.platformio'):
        for path in root.rglob('boot_app0.bin'):
            return path
    sys.exit('boot_app0.bin not found (build once with pio, or copy prebuilt/boot_app0.bin into the build dir)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('action', choices=('nvs', 'merge', 'check'))
    ap.add_argument('--project', default=str(DEFAULT_PROJECT), help='compiled TBD-PROJECT payload')
    ap.add_argument('--build-dir', help='PlatformIO build dir with bootloader/partitions/firmware.bin')
    ap.add_argument('--out')
    ap.add_argument('--pad-8mb', action='store_true', help='pad with 0xFF to a full 8 MB (QEMU twin flash)')
    ap.add_argument('--crosscheck', action='store_true')
    a = ap.parse_args()
    blob = project_payload(a.project)
    print(f'project payload: {len(blob)} bytes, sha256 {hashlib.sha256(blob).hexdigest()} '
          f'(what `project json` reports as payload_sha256)')
    nvs = nvs_partition(blob)
    if a.crosscheck:
        crosscheck(blob, nvs)
    if a.action == 'check':
        return
    if not a.out:
        sys.exit('--out is required')
    if a.action == 'nvs':
        Path(a.out).write_bytes(nvs)
    else:
        if not a.build_dir:
            sys.exit('--build-dir is required for merge')
        Path(a.out).write_bytes(merge(a.build_dir, nvs, a.pad_8mb))
    data = Path(a.out).read_bytes()
    print(f'wrote {a.out}: {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}')


if __name__ == '__main__':
    main()
