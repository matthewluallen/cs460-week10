# Prebuilt StampLC firmware — flash without compiling

These are **ready-to-flash images** of the StampLC teaching firmware, built from
`sbom/firmware/` in this repo (verified clean compile). Use these if you just want the
firmware on a device — **no toolchain, no build**. (To modify + rebuild instead, see
[`../BUILD.md`](../BUILD.md).)

| File | What it is |
|---|---|
| `stamplc-factory-8MB.bin` | **One-file image** — flash to offset `0x0` (bootloader + partitions + boot_app0 + app merged). Easiest. |
| `firmware.bin` | App only (offset `0x10000`) |
| `bootloader.bin` / `partitions.bin` / `boot_app0.bin` | The other parts (`0x0` / `0x8000` / `0xe000`) |
| `SHA256SUMS.txt` | Integrity — `sha256sum -c SHA256SUMS.txt` |
| `manifest.json` | ESP Web Tools descriptor (for a WebSerial flash button) |

Target: **ESP32-S3 (M5Stamp S3)**, 8 MB flash, DIO @ 80 MHz, custom OTA partition.

## Flash it

**A — one file with esptool (USB, any OS):**
```bash
pip install esptool            # once
esptool --chip esp32s3 write_flash 0x0 stamplc-factory-8MB.bin
```

**B — in the browser (WebSerial, Chrome/Edge):** use an ESP Web Tools install button
pointed at `manifest.json`, or M5Burner — pick `stamplc-factory-8MB.bin`, flash to `0x0`.
No local tools needed; needs a USB-connected board (so not from a cloud Codespace).

**C — separate parts (equivalent to A):**
```bash
esptool --chip esp32s3 write_flash \
  0x0 bootloader.bin  0x8000 partitions.bin  0xe000 boot_app0.bin  0x10000 firmware.bin
```

**D — PlatformIO (if you already have it):** `cd .. && pio run -e m5stamplc -t upload`.

After flashing: `esptool --chip esp32s3 run`, or just power-cycle. Serial console is
115200 baud.

## Notes
- A cloud Codespace can't reach USB — download the image and flash from a machine with
  the board (A/B/C), or use `pio run -t upload` on a local clone.
- Provenance: built from `sbom/firmware` at the repo's buildable commit (point-map schema
  `tbd.plc-point-map/v3`); re-verifiable with `pio run` (see `../BUILD.md`).
