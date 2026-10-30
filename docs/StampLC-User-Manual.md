# StampLC OpenPLC Controller — User Manual

**Model:** M5Stack StampLC (ESP32‑S3) · OpenPLC teaching firmware v1.0
**Audience:** instructors and students configuring and operating the device.

> **Please read first — intended use.** This is a **teaching and lab** controller. Its
> firmware deliberately contains known security weaknesses (documented in §11) so that
> students can find and fix them. **Do not use it to control real infrastructure, mains
> equipment, or anything where a failure matters, and do not place it on an untrusted
> or production network.** Operate it on an isolated lab segment only.

---

## 1. Overview

The StampLC is a compact programmable logic controller (PLC) built on an ESP32‑S3. It
provides 4 relay outputs, 8 opto‑isolated digital inputs, on‑board power and temperature
sensing, a real‑time clock, a 1.14" display, RS‑485, and an optional W5500 PoE Ethernet
port. It runs an OpenPLC‑style runtime: a small program scans inputs, evaluates logic, and
drives outputs on a fixed cycle. You configure and operate it over **USB‑C** (a serial
console) and, once networked, over **Wi‑Fi or Ethernet** (a web interface, Modbus, MQTT).

### What you can do with it
- Load and run **ladder / structured‑text** control logic.
- Read inputs and drive outputs locally or over a network.
- Speak industrial protocols: **Modbus TCP** (`:502`), a **Web HMI + JSON API** (`:8080`),
  **MQTT / Sparkplug B**, and a debug **telnet** console (`:23`).
- Inspect the device: live I/O, I²C bus scan, health diagnostics, an audit log.

---

## 2. Hardware at a glance

| Function | Part | Bus / address | Notes |
|---|---|---|---|
| Controller | M5StampS3 (ESP32‑S3) | — | Wi‑Fi + BLE, 8 MB flash |
| Relay/SSR outputs ×4 + opto inputs ×8 | AW9523B expander | I²C `0x59` | `relay0`–`relay3` (plus `ssr*`); the 8 opto DIs `di0`–`di7` are read on this chip too |
| Panel buttons ×3 | PI4IOE5V expander | I²C `0x43` | front buttons (A/B/C) + INT line |
| Power monitor | TI INA226 | I²C `0x40` | bus voltage / current |
| Temperature | NXP LM75 | I²C `0x48` | board temperature |
| Real‑time clock | RTC | I²C `0x32` | timestamps for the audit log |
| Display | 1.14" LCD | SPI | status screen |
| Wired network (optional) | WIZnet W5500 | SPI | PoE Ethernet add‑on |
| Serial field bus | RS‑485 | UART | Modbus RTU |

You can confirm this map on any unit with the `i2cscan` console command (§7).

> **Optional expansion module.** An add‑on I/O expansion module can be fitted on the
> internal I²C bus at `0x20–0x2F`. If it is **not** installed, the controller prints
> `[IOEXP] StamPLC IO module not found (0x20–0x2F)` at boot and runs normally on its
> built‑in I/O. This message is informational, not an error.

---

## 3. First‑time setup (USB‑C serial console)

All initial configuration happens over the USB‑C console.

1. Connect the StampLC to your computer with a USB‑C cable.
2. Open a serial terminal at **115200 baud, 8‑N‑1**. Any of these work:
   - macOS / Linux: `screen /dev/tty.usbmodem* 115200` (or `pio device monitor`)
   - Windows: PuTTY or Tera Term on the device's COM port
   - Browser (Chrome/Edge): any page built with the **Web Serial API** (see §8.4)
3. Press **Enter**. You should see the `plc>` prompt.

The console speaks plain text: **a line = a command + Enter** (LF or CR‑LF both work).

---

## 4. The configuration menu

Network and device settings are set from a short boot‑time menu.

1. **Reboot** the device (power‑cycle, or type `reboot` at the `plc>` prompt).
2. During boot it prints:
   `[CONFIG] Press ENTER within 5s for the config screen...`
   Press **Enter** within that window.
3. The menu appears:

```
  [1] WiFi OFF (contained, default)
  [2] WiFi STA  (join a network: SSID/pass)
  [9] WiFi AP   (setup mode: host a network + web config UI)
  [3] WiFi IP   (DHCP/static for STA)
  [0] ETH  IP   (W5500 DHCP/static, if present)
  [4] Read-only I/O self-test (sensors/DI/relay readback)
  [5] Show embedded CWEs & service ports
  [6] Save & boot
  [8] Toggle open-AP fallback (VULNERABLE if STA fails)
  [7] Factory-reset config
Choose:
```

Type a number and Enter. Settings are stored in non‑volatile memory and persist across
reboots. The menu idles out after 30 s and boots with the current configuration.

> By default the device ships **WiFi OFF ("contained")** — it will not join any network
> until you enable it here. This is deliberate for a lab device.

---

## 5. Network setup

The StampLC has **two independent network interfaces**, each with its own address:

- **WiFi** — always present (built into the ESP32-S3). Three modes: **Off** (contained), **STA** (join a network), **AP** (host a setup network).
- **W5500 Ethernet** — the optional PoE module; configured only when it is fitted.

Configure them three ways, all equivalent: the boot **config menu** (§4), the **`netcfg`** console commands (below), or the **web UI** (the Logic Studio ⚙ Network panel, or the on-device page at `http://192.168.4.1:8080/setup` in AP mode). Addressing changes apply on the next reboot.

> New to networking? Type **`explain <setting>`** at the `plc>` prompt (e.g. `explain gateway`, `explain dhcp`) for a plain-language description of any setting and what the firmware does with it, or **`learn`** for a guided walk (see §7).

### 5.1 `netcfg` — the console way

```
netcfg                          # show both interfaces + current state
netcfg json                     # the same, as JSON (what the web UI reads)
netcfg eth static <ip> <gw> <mask>    # W5500: fixed address
netcfg eth dhcp                       # W5500: automatic address
netcfg wifi sta <ssid> <pass>   # join a WiFi network (station mode)
netcfg wifi static <ip> <gw> <mask> | netcfg wifi dhcp   # WiFi addressing
netcfg wifi ap [<ssid> <pass>]  # host a setup network (default pass: stamplc-setup)
netcfg wifi off                 # no radio (contained)
netcfg ntp <ip> [port]          # time source (blank = use the gateway)
netcfg auth <user> <pass>       # change the web/API credentials (do this!)
netcfg save                     # persist to flash (then reboot to apply addressing)
```

### 5.2 Join a WiFi network (STA)

`netcfg wifi sta YOUR-SSID YOUR-PASS`, then `netcfg wifi dhcp` (recommended), `netcfg save`, and reboot. On boot the device connects and prints its address, e.g. `[WiFi] Connected! IP: 192.168.8.128` — note it for the web (`:8080`) and Modbus (`:502`) interfaces.

### 5.3 Setup / provisioning AP

To configure the device over WiFi with no cable and no terminal, put it in AP mode: `netcfg wifi ap` + `netcfg save` + reboot. It then hosts its own network — SSID **`StampLC-Setup-XXXX`** (XXXX = the device MAC), default password **`stamplc-setup`**. Connect a phone or laptop and open **`http://192.168.4.1:8080/setup`** to configure every setting in a browser.

> ⚠ The default AP password is a deliberate teaching weakness (**CWE-798/1392**, default credentials). Use AP mode only to provision, then switch to STA or Off — and change the password (`netcfg wifi ap <ssid> <newpass>`).

### 5.4 Wired Ethernet (optional PoE)

If the W5500 PoE module is fitted, configure it with `netcfg eth static …` or `netcfg eth dhcp`. If no module is present you will see `[ETH] hardware=0 link=0` at boot — normal on a WiFi-only unit, and the Ethernet settings are hidden in the web UI.

### 5.5 Default / contained mode

The device ships **WiFi OFF** (no network) — deliberate for a lab device. `netcfg wifi off` + `netcfg save` returns it to this fully-offline state.

---

## 6. Operating the controller

### 6.1 Run modes (key switch)

The controller has three modes, set with the `mode` command (and reflected by the physical
key switch):

| Mode | Command | Meaning |
|---|---|---|
| **PROGRAM** | `mode prog` | Outputs inhibited; you may download/replace the program. Safe default. |
| **RUN** | `mode run` | The stored program executes and drives outputs. Requires a valid program, ready inputs, and no faults. |
| **REMOTE** | `mode remote` | Allows remote control/download over the network. |

At boot the device stays in **PROGRAM** until you explicitly command RUN — outputs stay off
until then, by design.

### 6.2 Loading control logic

Write ladder or structured‑text logic in **OpenPLC Editor** (target: Arduino ESP32), or in
the browser **Logic Studio**, then load it to the device. The controller stores the last
validated program in non‑volatile memory and restores it on boot. Put the controller in
**PROGRAM** to download, then **RUN** to execute.

---

## 7. Console command reference

Type `help` at the `plc>` prompt for the live list. The most useful:

| Command | What it does |
|---|---|
| `status` | CPU mode, scan time, faults, forces, comms |
| `io` | digital inputs and relay commanded/actual state |
| `points` | list every named point and its live value |
| `show <tag>` | one point in detail |
| `set <tag> <val>` | write a writable point |
| `force <tag> <0\|1>` | override a coil/DI in the I/O image; `unforce <tag>` releases it |
| `mode run\|prog\|remote` | change run mode |
| `i2cscan` | probe the I²C bus (shows the chips in §2) |
| `diag [json]` | health snapshot (chip, heap, PLC, sensors) |
| `selftest` | quick hardware self‑test |
| `proto [name on\|off]` | list / enable / disable protocol services |
| `netcfg [json]` | per-interface network config: eth/wifi static\|dhcp, wifi sta\|ap, ntp, auth, save |
| `explain <setting>` | plain-language help for a config setting + what the firmware does (`explain` lists them) |
| `learn` | a guided walk through every config setting |
| `cwe` | list the embedded teaching weaknesses & service ports |
| `log` | the source‑attributed audit log |
| `bustrace on\|off` | retain decoded bus diagnostics for later review |
| `reboot` | software restart |

---

## 8. Network interfaces

### 8.1 Web HMI + JSON API (`:8080`)

Browse to `http://<device-ip>:8080/` for a simple operator page. The same data is available
as JSON for tools:

| Endpoint | Purpose |
|---|---|
| `GET /api/points` | all named points + values |
| `GET /api/set?tag=<t>&val=<v>` | write a point |
| `GET /api/force?tag=<t>&on=<0\|1>` | force a coil/DI |
| `GET /api/mode?set=run\|prog` | change mode |
| `GET /api/log` | audit log |
| `GET /api/coils`, `/api/registers` | raw Modbus image |

### 8.2 Modbus TCP (`:502`)

Standard Modbus/TCP. Coils 0–3 are the relay outputs; the input and register map is listed
with `points`. Any Modbus client (e.g. `pymodbus`) can read and write.

### 8.3 Telnet console (`:23`)

The same `plc>` console as USB, exposed over the network for remote diagnostics.

### 8.4 Browser console over USB (Web Serial)

Because the console is a USB‑serial device, a **Chrome or Edge** page using the Web Serial
API can present the `plc>` CLI in the browser with no terminal app — the student clicks
*Connect* and picks the port. (This requires the device plugged into that computer.)

---

## 9. I/O reference (point names)

Outputs (relays / SSR):

| Tag | Typical use |
|---|---|
| `relay0` | relay 0 (label it in your program, e.g. `motor`) |
| `relay1`, `relay2`, `relay3` | relays 1–3 (e.g. `light_green` / `light_yellow` / `light_red`) |
| `ssr1`, `ssr2` | solid‑state outputs |
| `estop` | E‑stop status lamp |

Inputs:

| Tag | Typical use |
|---|---|
| `di0_start` | start pushbutton |
| `di1_stop` | stop pushbutton |
| `di2_estop_in` | E‑stop contact |
| `di3` … `di7` | general digital inputs |

Sensors / status (read‑only): `vin_mv` (supply mV), `current_ma`, `temp_c_x10`
(°C ×10), `scan_last_us` / `scan_max_us` (cycle time), `fault_count`, `heartbeat`.

---

## 10. Protocols & services

Use `proto` to see the selectable services and turn them on or off, e.g.
`proto modbus on`, `proto mqtt off`. Available services include Modbus TCP, the Web HMI,
MQTT / Sparkplug B (northbound telemetry), and the debug telnet. Each service you enable is
an added network surface — enable only what the lab needs.

---

## 11. Security notice (teaching firmware)

This firmware **intentionally** ships with known weaknesses so students can practice finding
and remediating them. Type `cwe` on the console for the live list. They include, among
others: hardcoded Modbus credentials, cleartext Modbus, an unauthenticated E‑stop coil
write, unsigned OTA firmware updates, command injection in the diagnostic ping, an
unauthenticated debug telnet, a predictable session‑token generator, and a log‑viewer path
traversal. **Treat the device as hostile‑by‑design:** keep it on an isolated lab network,
never expose its raw services to the internet, and power it down when not in use.

---

## 12. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `[IOEXP] ... not found (0x20–0x2F)` at boot | The optional expansion module isn't fitted. **Normal** — the built‑in I/O still works. |
| `[ETH] hardware=0 link=0` | No W5500 PoE module. Normal on a Wi‑Fi‑only unit. |
| Wi‑Fi won't connect | Re‑enter SSID/pass in the config menu (§5.1); confirm the access point is up and on 2.4 GHz; check the printed IP. |
| Outputs won't actuate | The controller is in **PROGRAM** (outputs inhibited). Switch to **RUN** with a valid program loaded. |
| `? unknown tag` from `force`/`set` | Use a real point name from `points` (e.g. `relay0`, not a program label like `motor`). |
| No `plc>` prompt | Confirm 115200 8‑N‑1 and the right serial port; press Enter. |
| No hardware at all | The firmware can run in an emulator (QEMU) instead — see `docs/QEMU-Twin.md`. |

---

## 13. Quick reference

- **Console:** USB‑C, 115200 8‑N‑1, `plc>` prompt, `help` for commands.
- **Enter config:** reboot → press Enter within 5 s → menu.
- **Join Wi‑Fi:** menu `[2]` SSID/pass → `[3]` DHCP → `[6]` save.
- **Network services:** Web HMI `:8080`, Modbus `:502`, telnet `:23`.
- **Run it:** `mode run` (PROGRAM↔RUN); outputs off until RUN.
- **See the hardware:** `i2cscan`, `io`, `diag`.
