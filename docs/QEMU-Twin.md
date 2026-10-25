# Running the StampLC firmware in QEMU (no hardware)

**Audience:** instructors, and students who want to exercise the *real* firmware
without a physical StampLC. **Not required for the graded Week‑10 parts** (bus
analysis, SBOM, HBOM, ladder, and the remote live device are all reachable
without QEMU). This is the high‑fidelity "run the actual vulnerable firmware on
your own machine" path — useful for the firmware‑analysis and live‑service
(Modbus / HMI / telnet / console) enrichment, and for fully remote students who
have no bench access at all.

> **Why this document is dated.** This area accumulated several write‑ups over
> ~6 weeks, and earlier ones were superseded without being updated. The table
> below is the reconciled, *verified* picture as of the date shown; where an
> older note disagrees, this document wins. Read the **Status & provenance**
> section before trusting any other QEMU note you find.

---

## Status & provenance (read this first)

*Reconciled 2026‑10‑23 against file timestamps and the most recent emulator
evidence.*

| Approach | Dates seen | State | Verdict |
|---|---|---|---|
| **Patched‑QEMU twin** — Espressif `qemu` fork + StampLC device‑model patches; real 8 MB image boots to a live OpenPLC scan; `plc>` console + Modbus/TCP + I/O injection | patch set **2026‑09‑30**; live emulator evidence **2026‑10‑02 → 10‑04** (23 cases green on 10‑04) | **CURRENT / VERIFIED** | **Use this.** The only route proven to boot the unmodified firmware to live services. |
| Stock‑QEMU + `open_eth` + firmware HAL‑sim (early "license‑clean" idea) | **2026‑09‑03 → 09‑09** | Superseded | The committed `merged-eth.bin` is a *hardware* build and **hangs at `M5StamPLC.begin()`** under stock QEMU (confirmed 2026‑10‑23). The `[env:sim]` in that repo is a *native‑host* logic sim, not an ESP32 image. Do not rely on its `twin/qemu/README.md`. |
| "No W5500 → reach Modbus via USB‑CDC `gateway`" handoff note | **2026‑09‑30** | Stale | Pre‑dates the W5500 emulation ("Wall 8"). The current patched twin answers Modbus/TCP directly; the `gateway` command still exists but is no longer the only way in. |

**Bottom line:** the binary that runs our firmware is a **patched** build of
Espressif's `qemu-system-xtensa`, *not* a stock download. A stock
`qemu-system-xtensa` boots the ESP32‑S3 core but does **not** model the StampLC's
I²C chips (AW9523B, INA226, LM75B, RX8130, PI4IOE), the ST7789 LCD, or the W5500,
so the real firmware stalls during board bring‑up. (Empirically: a stock Espressif
`qemu-system-xtensa 9.2.2` loads ROM → 2nd‑stage bootloader → app entry, then goes
silent at `begin()`.)

---

## Quick start (prebuilt binary — recommended)

A de‑branded prebuilt twin for your platform is hosted on the repo's
`qemu-twin-v1` release (patched `qemu-system-xtensa` + its ROM + the 8 MB firmware
image). You don't need to build anything:

```bash
./qemu/fetch-qemu.sh     # downloads the binary + firmware image for your platform
./qemu/run.sh            # boots the firmware
nc -U /tmp/plc-serial    # (another shell) the plc> console
```

The rest of this document is for building the binary yourself or understanding how
the twin works.

---

## What you need

1. **A patched `qemu-system-xtensa`** (the TBD StampLC twin build). Built from
   Espressif's QEMU fork (`github.com/espressif/qemu`, branch `esp-develop`) with
   the StampLC device‑model patches applied (see **Building the patched QEMU**).
   The stock Espressif binary is the *base*; it is necessary but **not
   sufficient** on its own.
2. **An 8 MB merged flash image** of the firmware (see **Building the image**).
3. **`qemu/run.sh`** in this folder (the run wrapper).

> **Platform.** The verified emulator runs were produced on **Linux x86‑64** (the
> natural target for a Codespace or CI). A macOS (arm64) build of the patched
> QEMU also works for local use. Pick the build that matches where you'll run it.

---

## Building the image

From the firmware tree, build the flashed env and merge the artifacts into one
image. The firmware's image header declares an **8 MB** flash, so the merged
image must be **exactly 8 MB** or the S3 bootloader aborts (see Gotchas).

```bash
# in the firmware/ tree
pio run -e m5stamplc                      # the flashed env; match your ELF to this

# merge bootloader + partitions + app(+OTA) into one raw image
esptool.py --chip esp32s3 merge_bin -o build/flash.bin \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0      .pio/build/m5stamplc/bootloader.bin \
  0x8000   .pio/build/m5stamplc/partitions.bin \
  0xe000   <arduino>/tools/partitions/boot_app0.bin \
  0x10000  .pio/build/m5stamplc/firmware.bin
```

`merge_bin --flash_size 8MB` pads to 8 MB with `0xFF` (erased‑flash fill), which
is what QEMU wants. If you assemble the image by hand instead, pad to exactly
`0x800000` bytes. The NVS region is left blank so board detection and
default‑service flags run clean on every boot — **re‑copy a fresh image each run**
if your workflow writes NVS.

### Flash layout (Arduino‑ESP32, 8 MB)

```
0x00000  bootloader.bin
0x08000  partitions.bin
0x0e000  boot_app0.bin        (OTA-data)
0x10000  firmware.bin         (app0)
0x340000 (app1 / OTA slot)
0x670000 spiffs
0x7f0000 coredump
0x9000   nvs                  (blank in a fresh image)
```

---

## Running

```bash
FLASH_IMG=build/flash.bin ./qemu/run.sh
# plc> console on the serial socket; Modbus/TCP, HTTP-HMI, telnet as built
```

The verified invocation (what `run.sh` wraps) is:

```bash
qemu-system-xtensa \
  -M esp32s3 -m 8M \
  -drive file=build/flash.bin,if=mtd,format=raw \
  -global tbd-ioexp.firmware-revision=1 \
  -icount shift=3,align=off,sleep=off -accel tcg,thread=single \
  -serial null -serial null -serial unix:/tmp/plc-serial,server=on,wait=off \
  -qmp unix:/tmp/plc-qmp,server=on,wait=off \
  -nographic -monitor none -no-reboot
```

Notes on the flags that matter:

- **`-M esp32s3 -m 8M`** — the machine and RAM the patched build expects.
- **`-drive …,if=mtd,format=raw`** — the 8 MB image as the emulated SPI flash.
- **Three `-serial`** — the firmware's USB‑CDC operator console is `serial_hd(2)`,
  so the first two are `null` and the **third** carries the `plc>` console. Attach
  a terminal to that socket (`nc -U /tmp/plc-serial`, or `socat - UNIX:/tmp/plc-serial`).
- **`-global tbd-ioexp.firmware-revision=1`** — a device property of the twin's
  I/O‑expansion model. (If your build of the patch set uses a different identifier
  prefix for this property or the environment variables below, rename it to the
  `tbd-*` / `TBD_*` names used here so the binary and this doc stay in step.)
- **`-qmp unix:…`** — QMP control, used to inject inputs (see below).
- **`-icount … -accel tcg,thread=single`** — deterministic single‑thread timing;
  makes runs reproducible.

Optional environment variables:

| Variable | Effect |
|---|---|
| `TBD_W5_BIND` | host address the emulated W5500 binds its TCP services to (e.g. `127.0.0.1`) |
| `TBD_NOPHYS` | disable the internal process‑physics loop |
| `TBD_LCD_PPM` | dump the ST7789 framebuffer to a PPM/PNG |
| `TBD_BUZZER_LOG` | log decoded buzzer tone frequencies |

---

## Live interfaces under the twin

- **`plc>` console** (USB‑CDC on the 3rd serial) — the primary operator interface
  and, in the default *contained* config, often the only one. `help`, `status`,
  `io`, `points`, `force`, `mode run|prog`, `proto …`, `cwe`, etc. The prompt
  string `plc> ` is a reliable "ready for input" marker to sync on.
- **Modbus/TCP** — answered by the emulated **W5500** (on‑chip TCP/IP, 8 sockets
  bridged to host sockets). Binds to `TBD_W5_BIND` (e.g. `127.0.0.1:502`). An
  **unauthenticated** coil write moves the emulated process — the CWE‑306 lesson,
  with no hardware.
- **HTTP‑HMI / JSON API** — same W5500 path (`/api/points`, `/api/force`, …).
- **I/O injection via QMP** — drive inputs without physical wiring, e.g. set the
  AW9523 DI byte or the analog channels:
  ```
  # QMP qom-set against the emulated devices
  /machine/soc/aw9523   di = <byte>      # digital inputs
  /machine/soc/<analog>  ch1_mv / ch2_mv # IO-expansion field channels
  ```
  This is how the automated test harness drives 20+ process scenarios headlessly.

---

## Building the patched QEMU

The device‑model patches live in the TBD StampLC twin patch set
(`qemu-esp32s3-fixes/`). To produce the binary:

1. Check out Espressif's QEMU fork: `github.com/espressif/qemu`, branch
   `esp-develop`.
2. Drop each patched source over its matching path in the QEMU tree
   (`hw/xtensa/esp32s3.c`, `hw/i2c/esp32_i2c.c`, `hw/gpio/esp32_gpio.c`,
   `hw/misc/esp32c3_jtag.c`, plus headers).
3. Add the two StampLC I²C sources (`hw/i2c/*_i2c.c`) to `hw/i2c/meson.build`,
   guarded by `CONFIG_XTENSA_ESP32S3`.
4. Configure for `xtensa-softmmu` and build (`./configure --target-list=xtensa-softmmu && ninja -C build`).

The patch set models the StampLC base (AW9523B relays/DI @0x59, PI4IOE @0x43,
INA226 @0x40, LM75B @0x48, RX8130 @0x32, ST7789 LCD, buttons, buzzer, USB‑CDC
console, RS‑485, CAN), the **IO‑expansion pack** (@0x20), the **W5500 PoE pack**,
and a 100 ms process‑physics loop that ties relay outputs back to the sensors —
so an unauthenticated Modbus write physically moves the modeled process. It
closes 8 sequential "boot walls" (SAR‑ADC done bit, S3 I²C opcode remap, board
detect, LEDC abort, PI4IOE model, USB‑CDC TX, USB‑CDC host‑attach, and the W5500
+ Modbus/TCP path); the S3 I²C opcode remap is a genuine upstreamable QEMU bug
fix. The firmware image itself is **never** patched — every gap is closed in the
emulator, so the firmware "never knows it's a twin."

> Building QEMU from source is a heavy, one‑time step (not something students do
> in a Codespace). For class use, build the binary **once** per platform and
> vendor it, or host it for download; students then only need the image and
> `run.sh`.

---

## Gotchas (learned the hard way)

- **Flash size must match the header.** The image header declares 8 MB. A 2 MB
  image boots the ROM + 2nd‑stage bootloader, then aborts in a loop with
  `Detected size(2048k) smaller than the size in the binary image header(8192k).
  Probe failed.` Pad to exactly 8 MB. QEMU also only accepts 2/4/8/16 MB images.
- **Stock QEMU is not enough.** Stock `qemu-system-xtensa` has no StampLC
  peripheral models; the firmware stalls at `begin()` (board bring‑up) after the
  bootloader hands off. You need the patched build.
- **SLIRP "open" ≠ service up.** If you forward guest ports with
  `-nic user,hostfwd=…`, a bare TCP connect (`nc -z`) to the host port shows
  "open" because SLIRP accepts the host side even when nothing is listening in the
  guest. Confirm a service by reading a real response (e.g. `curl …/api/points`
  returning a body), not by a connect test.
- **Serial ordering.** The console is the **third** `-serial`. Put `null` on the
  first two or you'll attach your terminal to the wrong UART and see nothing.
- **Fresh NVS each run.** The image ships NVS blank on purpose (clean board
  detect + default service flags). If a run writes NVS, start the next run from a
  fresh copy of the image.
- **ELF ↔ image must match.** For symbol resolution (`addr2line`, gdb), use the
  `firmware.elf` built in the **same** env as the image (`m5stamplc`). A mismatched
  env ELF mis‑resolves symbols.

---

## De‑brand note

Student‑facing material in this repo uses **TBD** (testbed) throughout. If your
copy of the reference firmware or QEMU patch set carries legacy identifier
prefixes in places, they map to `tbd` / `TBD_*`. When you build the binary from
the patch set, apply the same rename so the `-global tbd-ioexp.*` property and
`TBD_*` env vars in this doc match the build.
