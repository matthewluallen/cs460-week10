# Building the two StampLC runtime profiles

The lab ships two firmware profiles built from the same source tree in this folder. They differ
only by a build switch.

| PlatformIO environment | Effective security switch | Purpose |
|---|---|---|
| `m5stamplc` | `ENABLE_CWE_DEMOS` defined; `SECURE_BUILD` absent | The teaching build — preserves the intentional vulnerable behavior for the investigation and remediation exercises. |
| `m5stamplc_secure` | `ENABLE_CWE_DEMOS` absent; `SECURE_BUILD` defined | The hardened build — selects the guarded remediation branch of each demo. The name does not certify complete security. |

Do not write `ENABLE_CWE_DEMOS=0` to select secure behavior: the source uses `#ifdef`, so a
definition with value zero still selects the teaching branch. `scripts/build_profile_check.py`
enforces that exactly one of the two switches is active and rejects the `-DFLAG=0` trap.

## Build from source

Install PlatformIO Core, then build the profile you want:

```bash
python3 -m pip install platformio
cd sbom/firmware
pio run -e m5stamplc            # teaching (vulnerable) profile
pio run -e m5stamplc_secure     # hardened (secure) profile
```

The first build downloads the pinned ESP32-S3 platform/toolchain and libraries (see
[`platformio.ini`](platformio.ini)) and needs network access. Outputs are separate:
`.pio/build/<env>/`, each with its own `firmware.bin` and `firmware.elf`.

A bare `pio run` produces an application `firmware.bin`, not a bootable image. To build a
bootable 8 MB twin image (bootloader + partitions + app merged at the documented offsets and
padded) and run it, use the twin wrapper:

```bash
./qemu/build-twin.sh secure     # compile + package the hardened 8 MB image
./qemu/run.sh secure            # boot it in the twin
```

## Existing prebuilt images

The committed [`prebuilt/`](prebuilt/) images and the `qemu-twin-*` release are the classroom
artifacts the lab runs by default. `prebuilt/SHA256SUMS.txt` records their hashes; the CI
`prebuilt-integrity` job checks them on every change.

An application-only `firmware.bin` is **not** an 8 MB factory/twin image. Do not pass it to
`qemu/run.sh` as `FLASH_IMG` or flash it at offset zero — a twin image must merge the matching
bootloader, partition table and boot metadata at the correct offsets and pad to the flash size
(that is what `qemu/build-twin.sh` does). Keep a clean factory image separate from the writable
image used for an individual test run.

## Local board upload

With a real M5StampLC connected over USB (not from a cloud Codespace — USB needs the board on the
machine running the tool):

```bash
cd sbom/firmware
pio run -e m5stamplc -t upload          # or -e m5stamplc_secure
pio device monitor -b 115200
```

The board target is ESP32-S3 / M5Stamp S3, 8 MB flash, DIO at 80 MHz. Before replacing firmware,
record the board's current firmware and keep a recoverable backup; the read-only
[bench capture](../../tools/bench/README.md) observes the existing console state without changing
it.

## What CI builds and checks

The CI workflow (`.github/workflows/ci.yml`) compiles and boots the committed image in the
`twin-smoke` job, runs the per-part assessment anchors (`lab-verify`), the Studio regression, the
bench capture tests, the prebuilt checksums, and the Level-3 Z3 proofs (`formal-verify`). You can
also run the Studio embed check locally: `node web/public/logic/embed.mjs --check`.

A successful build or host check is not a successful hardware test. See
[`../../remediation/README.md`](../../remediation/README.md) for the controls and their remaining
limits, and [`../../docs/QEMU-Twin.md`](../../docs/QEMU-Twin.md) for the twin.
