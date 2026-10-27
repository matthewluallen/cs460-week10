# CS460 Week 10 — Inside the Stack: StampLC SBOM/HBOM + Bus & Firmware Analysis

A hands-on lab on the **M5Stack StampLC**, a small real PLC (ESP32-S3; 4 relays,
8 opto-isolated inputs, RS-485, Modbus). You will read what a device is *made of*
(software and hardware bills of materials), follow a real **bus exchange** down into
the controller, and drive one **input → ladder logic → output** on a live device — all
from this repo.

Most of this lab runs **entirely in your browser or a container — no device required.**
The one live part (driving a real StampLC) is reached remotely through the class front
end; you never join the lab network.

## Open the lab

**GitHub Codespaces (recommended):** click **Code → Codespaces → Create codespace**.
Wait for the one-time setup to finish (installs the SBOM tools and fetches the firmware twin).

**Local VS Code:** install the **Dev Containers** extension, clone this repo, then
**Reopen in Container**. Same environment as Codespaces.

Either way, when setup finishes you'll have Node 20, Python 3.12, the SBOM tools, and the
QEMU firmware twin for Parts 6–7 — built for x86-64 (Codespaces) or arm64 (local Apple-silicon Mac).

## What you'll do

| # | Part | Folder | Device? |
|---|------|--------|---------|
| 1 | **Bus analysis** — decode a real recorded Modbus RTU exchange and reason about what it does (and doesn't) establish | [`bus-analysis/`](bus-analysis/) | No |
| 2 | **SBOM** — generate and compare software bills of materials (SPDX vs CycloneDX), then scan for CVEs | [`sbom/`](sbom/) | No |
| 3 | **HBOM** — map the StampLC's hardware components and reconcile them with what the bus actually shows | [`hbom/`](hbom/) | No |
| 4 | **Ladder logic** — start from a prebuilt program; enable one rung (or add one function) | [`ladder/`](ladder/) | No (apply remotely in Part 5) |
| 5 | **Wire & run** — one input → your logic → one output, on a real StampLC, driven remotely | [`wiring/`](wiring/) | Yes (remote) |
| 6 | **Firmware & the unsigned update** — boot the *real* firmware in the twin; find and prove an unsigned-OTA weakness | [`firmware-ota/`](firmware-ota/) | No (twin) |
| 7 | **Supply-chain kill switch** — detonate a dormant OTA payload through a compromised update source; trace it to a relay | [`supply-chain/`](supply-chain/) | No (twin) |
| 8 | **NTP adversary-in-the-middle & Y2038** — spoof the unauthenticated network time the controller trusts; wrap its 32-bit clock to 1902 | [`ntp-aitm/`](ntp-aitm/) | No (twin) |
| 9 | **Remediation — the secure build** — flip one compile switch (`-e m5stamplc_secure`); verify each CWE from Parts 6–8 is actually fixed, and which need an architecture, not a patch | [`remediation/`](remediation/) | No (twin) |

Work the parts in order; each builds on the last. Part 5 uses the class **front end**
(URL + your team code handed out in class) to toggle your team's input and watch the
output — the device stays on the lab bench, you stay remote.

## Browser tools (served from `web/public/`)

Several parts use small browser apps that run straight from this repo — no build step, no
lab network. Serve the folder once and open them on the forwarded port:

```bash
python3 -m http.server -d web/public 8000
```

| Tool | File | Where it fits |
|------|------|---------------|
| **TBD Logic Studio** — ladder / Structured-Text editor; compiles to the device IR; embedded **Bus monitor** | `web/public/logic_studio.html` | Part 4 |
| **Protocol Studio** — follow one captured or live frame field-by-field, from the wire up | `web/public/protocol_studio.html` | Parts 1 & 6 |
| **Operator panel** — the StampLC HMI / operator view | `web/public/panel.html` | Parts 6–7 |
| **Team Console** — pick a program and apply it to your team's PLC (the Part 5 front end) | `web/public/index.html` | Part 5 |

Logic Studio's **Bus monitor** and Part 1's I²C analyzer (`bus-analysis/i2c-analyzer.html`)
share one lane-timeline engine, so the same capture decodes identically in either — the
ladder editor and the bus analyzer are two views of the one device.

## How you'll be graded

You keep one short **finding** per part in your course notebook (a local text file is
fine). A finding states: **what the evidence shows, what it does *not* establish, your
uncertainty, and which next observation would decide a competing explanation** — plus
your sources and your individual contribution. A clean tool run is not a grade; the
reasoning is. See the assignment on Canvas for the rubric and the top-5 CVE / HBOM
tables to complete.

## The firmware twin (Parts 6–7)

Parts 6 and 7 run the **real, unmodified firmware** — the same `plc>` console and
Modbus / HMI / OTA services you'd meet on the bench — in an emulator, so you can boot it,
attack it, and watch the consequence on the I²C bus with **no hardware**. The dev
container fetches it for you (`qemu/fetch-qemu.sh`); start it with `./qemu/run.sh`.

Under the hood it's a **patched** build of Espressif's QEMU (a stock `qemu-system-xtensa`
boots the chip but can't model the StampLC's I²C sensors, LCD, or W5500). You never have
to build it — but if you're curious, **[docs/QEMU-Twin.md](docs/QEMU-Twin.md)** has the
full build recipe, the live interfaces, the dated provenance, and the gotchas.

**Building the firmware from source** (optional) is a separate, simpler path — the
controller source in [`sbom/firmware/`](sbom/firmware/) compiles with PlatformIO in
Codespaces or local VS Code. See [`sbom/firmware/BUILD.md`](sbom/firmware/BUILD.md).

## Configuring the device (instructors)

The StampLC's full setup and operation — USB‑C serial console, the boot config menu,
joining Wi‑Fi, run modes, the command reference, network interfaces, and the security
notice — is documented in **[docs/StampLC-User-Manual.md](docs/StampLC-User-Manual.md)**.
Students don't need it for the lab (the device is pre‑provisioned and reached remotely); it's
the product‑style manual for whoever sets up the kit.

## A habit to keep (it's the whole point)

Throughout, separate **what you observed** from **what you inferred**. A register value
is a number until the device map gives it meaning; a symbol name is not proof a function
ran; a CRC detects corruption but authenticates nothing. Say what you're sure of, and
what you'd measure next.
