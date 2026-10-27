# EXPERIMENTAL — dual USB-CDC analyzer (two serial ports on one cable)

**Status: compile-verified reference, NOT bench-tested, NOT the shipped firmware.**

## The problem this solves
The production analyzer (`../../src/main.cpp`) talks over the ESP32-S3 **USB-Serial/JTAG**
peripheral (`ARDUINO_USB_MODE=1`). That's a **single** CDC interface, so the OS gives exactly
one process the port — the WebSerial app (`cs460-cardputer-analyzer.netlify.app`) and a serial
client (pyserial, a terminal) **cannot both** be attached at once. The disconnect-tolerant
firmware (PR #3) makes the drop/reopen *clean*, but it does not make the port *shareable*.

## What this proves
The S3's other USB path — **TinyUSB** (`ARDUINO_USB_MODE=0`) with **`CFG_TUD_CDC=2`** — enumerates
a **composite device with two CDC-ACM interfaces**, i.e. **two `/dev/cu.usbmodem*` ports from one
USB cable**. This sketch services both independently (each with its own line buffer and echo), so
the web app can own port 0 while pyserial owns port 1 — simultaneously.

Compile-verified on the lab toolchain (pioarduino 55.03.37 / M5Cardputer): `pio run -e cardputer_dualcdc`.

## Try it on the bench
```bash
pio run -e cardputer_dualcdc -t upload      # flash a Cardputer
# two ports now appear, e.g. /dev/cu.usbmodemXXXX1 and /dev/cu.usbmodemXXXX3
# open each in its own terminal (or one in the web app, one in pyserial) and type 'help'
```

## Porting the full analyzer (the remaining work)
This is only the two-port plumbing. To make the real analyzer dual-port:
1. Switch its env to `ARDUINO_USB_MODE=0` + `CFG_TUD_CDC=2` (this file's build flags).
2. Replace the single `Serial` console with two `USBCDC` ports and route the existing command
   parser per-port: run `handle_serial()`/`bv_exec()` on whichever port received the line, writing
   replies back to **that** port (thread an output `Stream*` through `bv_exec`/`bv_stats`/`export_json`).
3. Re-validate the whole console on hardware — the USB stack changes from HWCDC to TinyUSB, which
   affects boot timing and the M5Cardputer display/USB init order (the prod firmware was tuned under
   HWCDC and had a prior boot-crash fix), so this MUST be bench-tested before classroom use.

Until then the production analyzer stays on the single-CDC, disconnect-tolerant build.
