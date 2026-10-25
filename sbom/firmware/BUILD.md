# Building / flashing the StampLC firmware

**You don't have to compile anything.** Prebuilt, ready-to-flash images live in
[`prebuilt/`](prebuilt/) — that's the fast path, and the Codespace opens with no
toolchain to install. Compiling is **optional** (below), only if you want to modify the
firmware.

## Flash the prebuilt image (recommended — no build)
Full steps in [`prebuilt/README.md`](prebuilt/README.md). Short version:
- **One file, esptool:** `esptool --chip esp32s3 write_flash 0x0 prebuilt/stamplc-factory-8MB.bin`
- **Browser (Chrome/Edge WebSerial):** an ESP Web Tools button on `prebuilt/manifest.json`, or M5Burner.
- **Verify first:** `sha256sum -c prebuilt/SHA256SUMS.txt`
A cloud Codespace has no USB, so download the image and flash from a machine with the board.

## Compile it yourself (optional / advanced)
Only if you want to change the firmware and rebuild. PlatformIO builds the same project on
any host (it fetches the right ESP32-S3 toolchain per OS):
- **Install PlatformIO** — in Codespaces or local VS Code, install the **PlatformIO IDE**
  extension (`platformio.platformio-ide`); or CLI `pip install platformio`. It's **not**
  installed by default, to keep the first Codespace run fast.
- **Build:** `cd sbom/firmware && pio run -e m5stamplc` — the first build pulls the ~1 GB
  ESP32-S3 toolchain once (needs internet; fine in Codespaces); later builds are quick.
- **Output:** `.pio/build/m5stamplc/` (`firmware.bin`, `bootloader.bin`, `partitions.bin`).

## Flash over USB (local, after building or to flash the prebuilt)
`pio run -e m5stamplc -t upload` (auto-detects port + offsets), then
`pio device monitor -b 115200`.

## Board
ESP32-S3 (M5Stamp S3), 8 MB flash, DIO @ 80 MHz, custom OTA partition (two OTA slots per
the preserved layout). The first-build toolchain is cached under `~/.platformio`.
