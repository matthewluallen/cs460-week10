# Reconstructed QEMU twin source (authoritative for `qemu-twin-v2`)

This directory holds the **authoritative source** that builds the StamPLC QEMU
twin shipped in the `qemu-twin-v2` release — the one `qemu/fetch-qemu.sh` pulls
and the labs run against. It exists so the binary is never un-rebuildable again:
the earlier twin's exact source was lost/scattered, which is what caused the
`/lcd` 404 regression when the release was reshipped from the wrong lineage.

## What it is
`qemu-esp32s3-fixes/` is the device-model overlay applied on top of
`espressif/qemu` (esp-develop / 9.2.2) by its own `apply_and_build.sh`:
patched `hw_xtensa/esp32s3.c`, the I2C/sensor models (`hw_i2c/tbd_i2c.*`,
`tbd_unit_identity.c`), `include/tbd_bridge.h`, the deadline/systimer patch
scripts, and the capture/test harness.

Feature set (all verified in CI qualification):
- ST7789 LCD PPM dump (`TBD_LCD_PPM`) → the real 240×135 screen on `/lcd`
- `analog-inputs` QOM property + ownership (`FIELD_ANALOG` / `TBD_NOPHYS`)
- `esp32_i2c_observe` bus-capture trace
- unit-identity, firmware-revision, `TBD_W5_BIND`

## Provenance / how it was reconstructed
- **Base:** the Oct-3 `cscoe-direct-ownership` lineage — the only tree that
  still had analog-inputs + observe + unit-identity together.
- **Rename:** case-preserving `cscoe → tbd` across contents and filenames, which
  is what makes the env/QOM names match the bridge contract (`CSCOE_LCD_PPM` →
  `TBD_LCD_PPM`, `CSCOE_W5_BIND` → `TBD_W5_BIND`, `CSCOE_NOPHYS` → `TBD_NOPHYS`;
  `analog-inputs` and `FIELD_ANALOG` carry no "cscoe" and are untouched).
- **Configure:** slim minimal-deps (`--disable-curl --disable-gnutls
  --disable-curses --disable-vnc --disable-gtk --disable-sdl --disable-png
  --disable-brlapi --disable-vte --disable-opengl --disable-spice`), static fdt,
  stripped — so the binary loads with only glib/pixman/slirp/gcrypt present.

## Builds
- **x86_64** was built this way in a Codespace and is live on `qemu-twin-v2`
  (sha256 `85c1ab63…`, 4,378,120 B), full CI green.
- **arm64** is built + qualified natively by `.github/workflows/twin-arm64.yml`
  on an `ubuntu-24.04-arm` runner. Dispatch it, confirm green, then
  `gh release upload qemu-twin-v2 qemu-twin-linux-arm64.tar.xz --clobber`.
