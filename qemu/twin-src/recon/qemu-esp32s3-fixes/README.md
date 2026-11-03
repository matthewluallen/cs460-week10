# TBD StamPLC Twin — Espressif QEMU device-model fixes

These files patch the Espressif QEMU fork (`github.com/espressif/qemu`, branch
`esp-develop`) so the **real, unmodified** M5Stack StamPLC firmware boots — and now
runs to a **live OpenPLC scan with an interactive console, answers Modbus/TCP on :502 over an
emulated W5500, and drives a process model an unauthenticated write can physically move** — on
the `esp32s3` machine. Twin philosophy: the firmware never knows it's a twin — nothing in the
firmware image is patched; every gap is closed in the emulator.

Drop each file over the matching path in a QEMU checkout, add the two TBD I²C
sources to `hw/i2c/meson.build` (guarded by `CONFIG_XTENSA_ESP32S3`), and rebuild.

## Emulated hardware stack (base + 2 expansion packs)

The twin models the real product as it ships — the StamPLC controller plus its two expansion
packs — so the unmodified firmware finds every board it probes for:

| Board | Modeled peripherals |
|---|---|
| **StamPLC base** (StampS3A / ESP32-S3) | AW9523 @0x59 (4 relays + 8 DIs), PI4IOE5V6408 @0x43 (buttons/LEDs/LCD backlight), INA226 @0x40 (device power), LM75B @0x48 (temp), RX8130 @0x32 (RTC), **ST7789 LCD framebuffer** (decoded from FSPI → the live HMI, dumpable to PNG via `TBD_LCD_PPM`), **front buttons A/B/C** (PI4IOE reg 0x0F, injectable over QMP), **LEDC buzzer with tone-frequency capture** (each `tone()` beep decoded + logged via `TBD_BUZZER_LOG`), the USB-C **USB-Serial-JTAG console** (Walls 1–7), the **RS-485 port** (UART1 → Modbus RTU, a 2nd Modbus surface when `proto modbus-rtu on`), and the **CAN/TWAI port** (SJA1000 controller, bus-attachable + a passive CAN monitor). Both **Grove units** are now emulated: **RED = BME680** env sensor (i2c[1] @0x77 → 24 °C/45 %/1013 hPa) and **BLUE = HX711** weight (GPIO 5/4 bit-bang → load tracks the process tank). |
| **IO expansion pack** (isolated, I²C @0x20) | `M5StamPLC_IO` register map: 2 field analog inputs (0–10 V / 0–20 mA, per-channel INA226 → Modbus regs 10–13), mechanical safety relay + 2 SSRs (`IO_CONTROL`), firmware-version / system-status / addr-config regs. Detected as *"IO module online at 0x20 (fw 0x11)"*. |
| **W5500 PoE pack** (SPI2 / FSPI, CS=GPIO11) | Full WIZnet W5500: common + 8 socket register blocks, on-chip TCP/IP bridged to host sockets → the firmware's Modbus/TCP :502 answers (Wall 8). |

A 100 ms process-physics loop ties them together: the base relays drive the base power/temperature
sensors *and* the IO-expansion field channels, so an unauthenticated Modbus write moves the whole
process. Run: `qemu-system-xtensa -M esp32s3 -m 8M -drive file=<8MB flash>,if=mtd,format=raw
-serial null -serial null -serial <console> -nographic` (the W5500 binds `127.0.0.1:502`).

## Files

| Banked path | QEMU path | What it adds |
|---|---|---|
| `hw_xtensa/esp32s3.c` | `hw/xtensa/esp32s3.c` | Machine wiring for everything below: 2 I²C controllers with the 6 StamPLC chips **+ the BME680 Grove unit on i2c[1] @0x77**, SAR-ADC/SENS done stub, GP-SPI board-ID stub, **LEDC buzzer tone-capture**, USB-Serial-JTAG chardev **+ its interrupt wired into the interrupt matrix**, the **W5500 Ethernet model** on FSPI (register file + 8 TCP sockets bridged to host sockets), the **ST7789 LCD framebuffer** capture, the **TWAI/CAN bus-link + passive CAN monitor**, and the **HX711** attach. |
| `hw_i2c/esp32_i2c.c` + `include/esp32_i2c.h` | `hw/i2c/esp32_i2c.c`, `include/hw/i2c/esp32_i2c.h` | **S3 I²C command-opcode remap** (`s3_mode`) — the load-bearing fix. |
| `hw_i2c/tbd_i2c.c/.h`, `hw_i2c/qemu_i2c_glue.c` | `hw/i2c/` | The StamPLC I²C device models — base AW9523/INA226/LM75B/RX8130/PI4IOE, the IO-expansion pack (0x20), **and the Grove BME680 (0x77)** — plus the internal process-physics loop (relays → base sensors + IO-exp field channels + `tbd_twin_weight_raw()` for the HX711). |
| `hw_gpio/esp32_gpio.c` + `include/esp32_gpio.h` | `hw/gpio/esp32_gpio.c`, `include/hw/gpio/esp32_gpio.h` | GPIO OUT/ENABLE latches, the **`GPIO_IN` (0x3C) input register**, and the **HX711 load-cell bit-bang model** (DOUT=5/SCK=4) with the self-healing frame state machine. |
| `hw_misc/esp32c3_jtag.c` + `include/esp32c3_jtag.h` | `hw/misc/esp32c3_jtag.c`, `include/hw/misc/esp32c3_jtag.h` | Full USB-Serial-JTAG **USB-CDC console**: interrupt-driven TX, RX FIFO from a chardev, and **emulated host-attach (SOF)** so the firmware's `Serial` comes alive. |
| `harness/run_*.py` | — | Repro/verification harnesses: `run_console`/`run_hd2` (console capture), `run_modbus`/`run_physics` (Modbus + attack chain), `run_rs485` (Modbus RTU), `run_lcd`/`run_btn` (HMI), **`run_bme`** (Grove env), **`run_hx_process`** (HX711 weight tracking the pump), **`run_can_bind`/`run_can_qtest`** (CAN bind + end-to-end frame), **`run_buzzer`** (tone capture). |

## The boot "walls" — what each fix unblocked

The stock fork gets the firmware nowhere near `app_main`. These are the sequential
hangs/aborts that had to be modeled, in the order the firmware hits them:

1. **SAR-ADC done bit** — M5Unified's ADC calibration polls `SENS 0x6000880C bit16`
   forever. RAM-backed SENS/APB_SARADC stub forces the done bit + a result. (esp32s3.c)
2. **S3 I²C command opcodes** — the fork's `esp32_i2c.c` hardcodes the *original*
   ESP32 opcode numbering. The S3 renumbered them: `RSTART 0→6, READ 2→3, STOP 3→2`
   (`WRITE 1`/`END 4` unchanged — which is why it *looks* half-working). `s3_mode`
   remaps them. Without this, reads return garbage and writes never complete.
   (esp32_i2c.c/.h) — *a genuine, upstreamable S3 QEMU bug fix.*
3. **Board detection** — M5GFX probes the ST7789 LCD ID over GP-SPI with the panel
   CS driven from a GPIO. Needed (a) a GP-SPI ID stub that answers only when GPIO12
   (StamPLC LCD CS) is driven low, and (b) real GPIO OUT/ENABLE latches so the stub
   can see the CS. Result: board auto-detects as **21 (StamPLC)**. (esp32s3.c, esp32_gpio.c)
4. **LEDC abort** — `ledc_ll_get_slow_clk_sel` calls `abort()` if `LEDC_CONF.apb_clk_sel`
   reads 0. RAM-backed LEDC stub (writes persist) clears the buzzer-setup abort. (esp32s3.c)
5. **PI4IOE5V6408 @0x43** — the button / status-LED / LCD-backlight I²C expander.
   Unmodeled → every access NACKs → the LovyanGFX I²C context latches
   `connection_lost` and the driver spins in bit-banged recovery. New RAM-backed
   register-file model (device-ID reg 0x01 = 0xA0 so `begin()` detects it; input
   status 0x0F idle-high). (tbd_i2c/qemu_i2c_glue.c, esp32s3.c)
6. **USB-Serial-JTAG TX path** — `ARDUINO_USB_MODE=1` routes the firmware's `Serial`
   to the USB-Serial-JTAG peripheral, whose model was a pure stub that silently
   dropped every byte. First fix: forward TX writes (`EP1_REG`) to `serial_hd(2)`.
   Necessary but **not sufficient** — see Wall 7. (esp32c3_jtag.c, esp32s3.c)
7. **USB-CDC host-attach + interrupt-driven console (the decisive one).** Even with
   TX forwarding, the console stayed silent and the OpenPLC scan never ran. The real
   firmware treats the USB-C port as its primary (and, in the default *contained*
   config, its **only**) operator interface, and it drives that port the way the
   Arduino core really does — nothing about it is a stub:
   - **TX is interrupt-driven.** `Serial.write()` fills a ring buffer and relies on
     the `SERIAL_IN_EMPTY` interrupt to flush bytes to `EP1_REG`. The model must
     latch `IN_EMPTY` as an **edge** — set on packet flush (`WR_DONE`) and when the
     driver enables the interrupt while the IN endpoint is empty, cleared by
     `INT_CLR`. (A permanent *level* storms the early IDF console into a hang;
     suppressing it entirely strands every `Serial.print` in the ring — both were
     dead ends before the edge model.)
   - **Host-attach is inferred from SOF.** `HWCDC` only transmits when
     `usb_serial_jtag_is_connected()` is true, and with `setTxTimeoutMs(0)` it
     *drops* output when it's false. That flag is maintained by a FreeRTOS tick hook
     (`usb_serial_jtag_connection_monitor.c`) that polls the **SOF interrupt raw
     bit**: a real USB host emits a Start-of-Frame every 1 ms, so the bit is set
     every tick. With no host, the port latches *disconnected* after ~3 ms and the
     console goes dark forever. The twin reports the **SOF raw bit as set** (in the
     `INT_RAW` read only — never stored, never delivered as a CPU IRQ, so it can't
     storm), i.e. it presents an always-attached host.
   - The JTAG interrupt is wired into the interrupt matrix at
     `ETS_USB_SERIAL_JTAG_INTR_SOURCE` so the HWCDC ISR actually fires. (esp32s3.c)

   Together these turn the USB-C port into a working bidirectional console *and*
   unblock the main loop. (esp32c3_jtag.c/.h, esp32s3.c)

8. **W5500 Ethernet + Modbus/TCP (the network path).** This firmware image is built
   `-DENABLE_ETHERNET` and drives the StamPLC PoE module's WIZnet **W5500** over FSPI
   (SPI2: SCK7 MISO9 MOSI8, software chip-select on **GPIO11**) via the
   `Ethernet_Generic` library — which uses the W5500's **on-chip TCP/IP stack** (8
   hardware sockets), not MACRAW. With no W5500 model the driver reports
   `hardware=0`, `g_net_up` stays false, and every TCP service (Modbus/502, HTTP-HMI,
   …) is skipped. The twin adds a register-level W5500 model on FSPI:
   - **SPI framing.** VDM: each CS-low session is `[addr_hi][addr_lo][control][data…]`,
     `control = (BSB<<3)|(RWB<<2)|OM`; the model decodes the on-wire control byte
     exactly as the chip does. FSPI mechanics learned by capture: MOSI packed LE in
     `W0..W15`, bit-length in `SPI_MS_DLEN` (0x1C), and the transfer starts on **SPI_USR
     (bit 24)** — *not* SPI_UPDATE (bit 23), which the driver also writes; triggering on
     both ran every burst through the model twice and desynced the frame. Frames are
     delimited by the **GPIO11 CS edge**, fed to the model from the GPIO block via
     `tbd_w5500_cs_notify()`. MR (0x00) bit7 self-clears so `softReset()`'s poll ends;
     VERSIONR (0x39)=0x04, PHYCFGR (0x2E) link-up.
   - **Sockets bridged to the host.** Each W5500 hardware socket maps to a real host
     TCP socket: `Sn_CR` OPEN/LISTEN/SEND/RECV/CLOSE execute and self-clear; a TCP
     LISTEN on the firmware's port opens a host listener on `127.0.0.1:<port>`; an
     inbound connection lands in the socket's RX ring (`Sn_RX_RSR`/`Sn_IR` RECV) and
     SEND flushes the TX ring to the peer. MMIO (vCPU) and the fd callbacks (main loop)
     both hold the BQL, so shared state needs no extra locking. (esp32s3.c)

## Deeper-fidelity peripherals (Grove units, CAN, buzzer)

Beyond the boot walls, these close the remaining gaps between the twin and a fully-optioned
physical StamPLC. Each was validated against the **unmodified** firmware.

- **Grove RED — BME680 env unit (i2c[1] @0x77).** A pointer+auto-increment I²C slave seeded with a
  42-byte calibration set + a field-data block, solved off-line (`bme_solve.py`) so the firmware's
  *own* Bosch bme68x FPU compensation lands on a fixed, sane environment. Chip-ID reads 0x61,
  soft-reset (0xE0←0xB6) is a no-op, `new_data` is held set so every forced-mode read returns
  immediately. Firmware prints `[GROVE] BME688 online on RED port (0x77)` and
  `temp=24.0C rh=45% p=1013.0hPa (env_ok=1)`.
- **Grove BLUE — HX711 load cell (GPIO DOUT=5 / SCK=4).** A GPIO bit-bang peer reproducing the full
  HX711 protocol signature the firmware's `hx711_try()` checks: DOUT idles high, drops low when a
  sample is ready, shifts 24 bits MSB-first (one per SCK rising edge, read while SCK is high), then
  returns high after the 25th (gain) pulse. Readiness is armed on a ~80 Hz virtual-clock timer *and*
  lazily on GPIO_IN reads; a partial frame with no clock activity for >2 ms is abandoned and
  re-armed (needed because stray GPIO4 toggles at boot were clocking the frame partway and sticking
  it). The 24-bit sample is the **process tank weight** (`tbd_twin_weight_raw()`), so it rises
  when the pump runs and drains on e-stop. Required adding the **`GPIO_IN` (0x3C) input register** to
  the ESP32 GPIO model (previously undecoded → always read 0). Firmware prints
  `[GROVE] HX711 online on BLUE port (DAT=5 CLK=4) raw=…`.
- **CAN / TWAI (TX42/RX43) + passive bus monitor.** The `esp32_twai.c`/SJA1000 controller is
  present, IRQ-wired, and binds to `-object can-bus,id=canbus0` — the real image boots clean with a
  bus attached. A new passive **CAN monitor** (`CanBusClientState` on the same bus) captures every
  transmitted frame to `TBD_CAN_LOG`. Bring-up driven via **qtest** (`run_can_qtest.py`) programs
  the SJA1000 (PeliCAN, self-test mode) and transmits `id=0x123 dlc=2 AB CD`; the monitor logs it —
  the guest→bus transmit path proven end-to-end. **Note:** the shipped firmware has *no* CAN feature
  (there is no `ENABLE_CAN_BUS` build and nothing drives the TWAI), and the sandbox can't create a
  host `vcan`, so the RX direction awaits a CAN-using lab scenario + peer.
- **Buzzer (LEDC, GPIO44) tone capture.** `tone()` → LEDC; the model now decodes the tone frequency
  from `LEDC_TIMERn_CONF` (DUTY_RES[3:0] / CLK_DIV Q10.8[21:4], XTAL 40 MHz source) and logs each
  beep. Verified: the boot `tone(1500,90)` decodes to **~1500 Hz**. Every beep re-asserts a non-zero
  duty (the channel stays attached), so the model triggers on the duty write; the LCD backlight is
  on the PI4IOE (not LEDC), so LEDC traffic is buzzer-only.

## Verified live state (after walls 1–8)

Booting the **unmodified** `merged-tbd.bin` (env `m5stamplc`) on `-M esp32s3 -m 8M`
with `serial_hd(2)` on a socket, captured over the USB-CDC console:

```
[ETH] W5500 via Arduino Ethernet library (SPI2 SCK7 MISO9 MOSI8 CS11)...
[ETH] hardware=3 link=1 ip=10.13.37.10
[ETH] network up at 10.13.37.10 - starting services
[ETH] sockets: 0=14/502 1=14/502 2=14/502 ...        (0x14 = SOCK_LISTEN on :502)
[Modbus] TCP server started on port 502
===== Startup complete =====
[console] live PLC debug terminal ready. Type 'help'.
plc> [PLC] mode=RUN scan=21260us(max 48551) faults=0 forces=0 estop=0 | CH1=0mV/0uA CH2=0mV/0uA
[Status] Uptime: 25s | Cycles: 0 | WiFi: 0 dBm | Heap: 270228 | LoopStackFree: 5144
```

**Modbus/TCP over the emulated W5500** — a host client on `127.0.0.1:502` (one persistent
connection, as a real Modbus master polls) against the unmodified firmware:

```
read holding regs 0-5  → fc=0x3  [0,0,0,0,0,0]      (idle until physics / NS-3)
read coils 0-7         → fc=0x1  0x00
WRITE coil0 = ON       → fc=0x5  OK   (unauthenticated — CWE-306)
re-read coils 0-7      → fc=0x1  0x01                (the write took effect)
```

The server implements FC 1/3/5/6/15/16; FC02/FC04 get no reply (`default: return false`
in `modbus_tcp_server.cpp`), so the twin dropping them mirrors the real device exactly.

**Process physics — unauth write → physical consequence (the full attack chain).** An internal
process model (in `qemu_i2c_glue.c`) makes the sensors respond to the firmware's own relay
outputs, matching the shipped "default process" ladder (`QX0`=motor, over-temp interlock `MX4`
at 40 °C). Driving it entirely over unauthenticated Modbus:

```
baseline (idle)                 Vin=24.00V  I=39mA   temp=22.0C  relays off
write coil 9 (cmd_run) = ON  →  Vin=23.57V  I=531mA  temp→42C    motor runs   ← CWE-306
   …temp crosses 40 °C        →  I=52mA      temp 37C            interlock MX4 trips
   …cools, cmd_run re-latches →  I=531mA                          (thermal cycling)
write coil 8 (soft_estop)= ON→  I=52mA       temp→22C            process halted, red light
```

So an attacker with only unauthenticated Modbus/TCP starts and stops a motor and the emulated
current/voltage/temperature move accordingly — and the ladder's safety interlock behaves. This
closes all four sprint-oracle checks. The physics is internal by default; set **`TBD_NOPHYS`**
to run the sensors flat and let an external bridge drive them via QMP `qom-set` (the models expose
`ina226 bus_mv`/`current_ma`, `lm75b temp_mc`, and `aw9523 relays`/`di`).

- **`setup()` runs to completion** (config menu → WiFi-off/contained → OpenPLC init/start
  → default 30-instr process loaded → "Startup complete").
- **The OpenPLC scan cycle runs continuously** — `mode=RUN`, per-scan time reported
  (~20–40 ms under emulation, well under the 50 ms cycle and the 8 s loop-WDT),
  `scans == hb` (heartbeat tracks scans 1:1), `faults=0`.
- **The console is interactive both ways.** Typing `help`, `svcinfo`, `status` into the
  port returns the full teaching-firmware shell — including the reachable embedded
  weaknesses (`service pw SRV-XXXXXX = last 3 MAC octets XOR 0x2A`,
  `user pw 1234 [CWE-257/256]`).
- **Stable**: uptime advances monotonically, heap flat (~270 KB), no watchdog reboot.

Debugging method that actually worked (reusable, and a correction of earlier notes):
- **Capture both console streams to files.** The ESP-ROM/2nd-stage banner and the
  ARDUHAL/IDF `[ts][level][file]` logs use the *polling* console and appear on **both**
  UART0 (`serial_hd(0)`) and the JTAG port; the firmware's own `Serial.*` uses HWCDC and
  appears **only** on `serial_hd(2)`. Wiring `serial_hd(2)` to `null` (as an early
  harness did) hides exactly the stream you care about — the absence of any `Serial.*`
  line while IDF logs flow is the tell that HWCDC TX is broken, not that `setup()` hung.
- **Read the firmware source, don't lean on `addr2line`.** In optimized/inlined code it
  mis-resolves (it sent an earlier investigation chasing a non-existent
  `plc_console_poll` RX-spin). The wedge was the *whole loop* not running because
  `setup()` never returned useful console state — a source read of `main.cpp` /
  `program_loader.cpp` settled it in minutes.
- **Match the ELF to the running image.** The running image is env `m5stamplc`; the only
  ELF on the build host was env `m5stamplc-eth` — a different memory map, so hardcoded
  global addresses read garbage. Rebuild the exact env or read globals by symbol from
  the matching ELF.
- For genuine memory probing: boot under `-qmp unix:...`, read globals live via
  `xp`; Z2 data watchpoints work, Z0/Z1 breakpoints do not (Z0 can't patch XIP flash,
  Z1 isn't wired in this fork).

## Known-remaining (not yet modeled)

- **Peripherals the firmware doesn't use (documented, not gaps).** SD card — the image uses
  SPIFFS/LittleFS in internal flash, there is no physical SD to model. RTC RX8130 (0x32) — detected,
  but the firmware keeps its black-box in ESP32 RTC-slow RAM and takes wall-clock from system/SNTP,
  so real BCD timekeeping would be unobservable; the stub suffices. Alert LED (GPIO21) — never
  driven. BOOT/G0 hold-to-flash — a download-mode strap, not read at runtime and moot in QEMU.
- **CAN RX direction.** The transmit path is proven to the bus; receiving into the guest needs a
  CAN-using firmware scenario and an external peer (a host `vcan`, unavailable in the build sandbox).
- **W5500 socket lifecycle polish.** TCP LISTEN/accept/RECV/SEND/CLOSE and the Modbus
  path work (see above); rapid *new* connections can outrun the firmware's listener
  replenishment (a persistent connection is unaffected). UDP and TCP-client (CONNECT)
  modes are stubbed — add if a lab needs the HTTP-HMI's outbound calls or a UDP service.
- **HTTP-HMI :8080 and other TCP services** ride the same W5500 bridge and should work
  once exercised; only Modbus/502 has been driven end-to-end so far.
- **IO-expansion analog channels are illustrative.** The module is modeled (detected at
  0x20, fw 0x11, `SYSTEM_STATUS`=ready) and its two field channels carry live process
  values (CH1 tank level 0–10 V, CH2 flow 4–20 mA → Modbus regs 10–13); the module's PWM
  outputs and per-channel INA226 config registers are stored but not simulated. Its
  mechanical relay + 2 SSRs are writable (`IO_CONTROL` @0x10) and the firmware drives them
  from coils 0/5/6, but they don't yet feed back into the physics.

## Hardware reference (authoritative, from M5Stack sources)

Internal I²C = **I2C_NUM_0, SDA=G13/SCL=G15, 400 kHz**. Devices: AW9523B **0x59**
(4 relays P0.0–3 + 8 DIs P0.4–7/P1.4–7; push-pull must be enabled), PI4IOE5V6408
**0x43** (3 buttons, status LEDs, LCD backlight P7), INA226 **0x40**, LM75B **0x48**,
RX8130CE **0x32**. Shared **RST = G3** (LCD + PI4IOE + PoE; also a strapping pin — must
be HIGH at boot) and shared **INT = G14** (PI4IOE + W5500). SPI (SCLK7/MOSI8/MISO9):
LCD CS=G12, SD CS=G10, W5500 CS=G11. Buzzer = G44 (LEDC). USB-C = native USB-Serial-JTAG
(`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`) — the firmware's `Serial`, its
console, and its `PLCLOAD` program-upload channel (2 KB RX buffer). Module = Stamp-S3A
(ESP32-S3FN8, 8 MB flash, no PSRAM).

### September13 Modbus acceptance corrections

The W5500 listener bridge accepts only when a matching guest LISTEN socket is
available and publishes the incoming peer address/port in socket registers.
The UART overlay `hw_char/esp32_uart.c` replaces FIFO-sized RX bursts and pauses
with one byte per configured8N1 character interval, retaining FIFO backpressure.
This prevents the emulator from inventing inter-frame gaps in maximum Modbus
RTU requests. It is RX character pacing, not electrical or TX timing validation.
The owned-network live probe is `firmware/tests/qemu_modbus_bounds_probe.py`;
its artifact checks require the supplied app to be embedded in the test image.
See `docs/TBD-MODBUS-BOUNDS.md` for physical and emulator evidence and limits.
