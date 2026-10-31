# CS460 Week 10 — Inside the Stack: StampLC SBOM/HBOM + Bus & Firmware Analysis

A hands-on lab on the **M5Stack StampLC**, a small real PLC (ESP32-S3; 4 relays,
8 opto-isolated inputs, RS-485, Modbus). You will read what a device is *made of*
(software and hardware bills of materials), follow a real **bus exchange** down into
the controller, and drive one **input → ladder logic → output** on a live device — all
from this repo.

The whole lab runs **in your browser and the QEMU firmware twin — no hardware required.**
The twin boots the *real, unmodified* StampLC firmware, so you run it, drive it, and attack
it exactly as you would a device over USB. A physical StampLC on the bench validated every
step on real silicon; it appears here only as reference ("this is what it looks like on
hardware"), never as something you need to own or connect to.

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
| 4 | **Ladder logic** — start from a prebuilt program; enable one rung (or add one function) | [`ladder/`](ladder/) | No (apply on your twin in Part 5) |
| 5 | **Wire & run** — one input → your logic → one output, driven from the **Logic Studio** against **your own twin** (every step validated on a real StampLC too) | [`wiring/`](wiring/) | Twin |
| 6 | **Firmware & the unsigned update** — boot the *real* firmware in the twin; find and prove an unsigned-OTA weakness | [`firmware-ota/`](firmware-ota/) | No (twin) |
| 7 | **Supply-chain kill switch** — detonate a dormant OTA payload through a compromised update source; trace it to a relay | [`supply-chain/`](supply-chain/) | No (twin) |
| 8 | **NTP adversary-in-the-middle & Y2038** — spoof the unauthenticated network time the controller trusts; wrap its 32-bit clock to 1902 | [`ntp-aitm/`](ntp-aitm/) | No (twin) |
| 9 | **Direct remediation and compensating controls** — preserve the vulnerable/secure build switch, compare a specific changed path or exposure control, and record its evidence and remaining limits | [`remediation/`](remediation/) | No (twin) |

Work the parts in order; each builds on the last. In Part 5 you run **your own twin** and
drive it from the **Logic Studio** in your browser — no teams, no shared bench, no hardware:

```bash
./qemu/studio.sh          # starts your twin + serves the Studio on port 8080
```

Then open the forwarded port **8080** (Codespaces pops it up; locally it's
`http://localhost:8080`) and click **Connect twin**. The one server both serves the page and
bridges it to your twin, so the connection is same-origin — nothing to configure.

> **Have a real M5StampLC?** The *same* Studio drives it over USB: open it on your own
> machine (`http://localhost:8080` via a local Dev Container, or any HTTPS copy) in
> **Chrome or Edge**, plug in the board, and click **Connect USB** instead of Connect twin.
> (USB needs the device on the machine running the browser, so this path doesn't work from a
> cloud Codespace — use the twin there. Every step is validated identically on real silicon.)

### Extensions (Weeks 10–11)

Three short extensions deepen the core parts above. Do each after the part it builds on; they
are graded the same way and their objectives + MITRE/CWE mapping are in
[`assessment/objectives.md`](assessment/objectives.md).

| Ext | Builds on | What you do | Where |
|---|---|---|---|
| **E1 — Provenance Studio** | 1 · 5 · 9 | Match a live `/api/log` event to the firmware **source line + CWE**, and spot the **provenance gap** — an output change with no WRITE/FORCE row (the signature of an unaudited, memory-corruption write) | [`web/public/provenance_studio.html`](web/public/provenance_studio.html) |
| **E2 — Signal Journey** | 1 · 4 · 5 · 7 | Follow one byte across five lenses — Wireshark → sigrok → firmware source → ladder → relay — and say what each lens can and cannot show | [`web/public/signal_journey.html`](web/public/signal_journey.html) |
| **E3 — Phone-as-sensor false-data injection** | 5 · 8 | Withhold a sensor the PLC trusts (availability) and spoof its value (integrity), then build a plausibility gate that still passes the real reading | [`docs/assignment-phone-sensor-fdi.html`](docs/assignment-phone-sensor-fdi.html) |

## Browser tools (served from `web/public/`)

Several parts use small browser apps that run straight from this repo — no build step, no
lab network. `./qemu/studio.sh` already serves this folder on port 8080; for the no-device
tools alone you can instead run:

```bash
./tools/serve-web.sh    # serves web/public; open the forwarded port in your browser
```

| Tool | File | Where it fits |
|------|------|---------------|
| **TBD Logic Studio** — ladder / Structured Text authoring; drives **your twin** (Connect twin) or a **real board over USB** (Connect USB); embedded **Bus monitor** | `web/public/logic_studio.html` | Parts 4 & 5 |
| **Protocol Studio** — follow one captured or live frame field-by-field, from the wire up | `web/public/protocol_studio.html` | Parts 1 & 6 |
| **Operator panel** — the StampLC HMI / operator view | `web/public/panel.html` | Parts 6–7 |

Logic Studio's **Bus monitor** and Part 1's I²C analyzer (`bus-analysis/i2c-analyzer.html`)
share one lane-timeline engine, so the same capture decodes identically in either — the
ladder editor and the bus analyzer are two views of the one device.

### Generic application workflow under review

The private generic-runtime draft separates the firmware from application logic,
tag names, mappings and defaults. In Studio, **Open project → inspect and validate →
PROGRAM → Load application → Read installed application → RUN**. Both vulnerable
and secure firmware use this same contract. A newly loaded application stays stopped.
Reboot defaults to PROGRAM; an intentionally added startup ST declaration or ladder
rung can request RUN on a later reboot after readiness checks.

- [Prepared projects and deployment contract](docs/GENERIC-RUNTIME.md)
- [RUN, PROGRAM, REMOTE, startup and optional per-host point permissions](docs/OPERATING-MODES-AND-ACCESS.md)
- [Expansion-module unit-test coverage and its limits](sbom/firmware/tests/EXPANSION-COVERAGE.md)
- [PLC capability review and implementation priorities](docs/PLC-CAPABILITY-ROADMAP.md)
- [Approved implementation sequence and acceptance requirements](docs/APPROVED-IMPLEMENTATION-PLAN.md)
- [Initial read-only capture from a normally booted local board](tools/bench/README.md)
- [Local USB installation and bench handoff for Claude](docs/CLAUDE-USB-HANDOFF.md), with the [pinned two-profile candidate](docs/USB-BENCH-CANDIDATE.json)
- [Execution timing, watchdog results and historical first-fault evidence](docs/EXECUTION-DIAGNOSTICS.md)
- [Exact-artifact QEMU physics, loadable plant models and replay](docs/QEMU-Twin.md)

This draft does not replace the published classroom binaries or the earlier
fixed-application exercises until the reviewed student export is released. Use the
exact firmware/project combination named by the exercise; the legacy Team Console
and prepared generic-project loader have different deployment contracts.

## How you'll be graded

You keep one short **finding** per part in your course notebook (a local text file is
fine). A finding states: **what the evidence shows, what it does *not* establish, your
uncertainty, and which next observation would decide a competing explanation** — plus
your sources and your individual contribution. A clean tool run is not a grade; the
reasoning is.

The **measurable learning objectives** and the **rubric** live in the repo under
[`assessment/`](assessment/) — each objective is tagged where it can be shown (physical PLC,
twin, or offline) and mapped to a MITRE ATT&CK-for-ICS technique and a CWE. Each part also has
a machine-checkable anchor you (and your grader) can run on either target:

```bash
./qemu/run.sh                     # boot the twin (or plug in a StampLC)
python assessment/verify-lab.py   # PASS/FAIL/SKIP per part — the "mechanics" half of the rubric
```

See the assignment on Canvas for the top-5 CVE / HBOM tables to complete.

## The firmware twin — your test bench (no hardware)

The twin runs the **real, unmodified firmware** — the same `plc>` console and
Modbus / HMI / OTA services you'd meet on the bench — in an emulator, so you can boot it,
drive it, attack it, and watch the consequence on the I²C bus with **no hardware**. The dev
container fetches it for you (`qemu/fetch-qemu.sh`).

**Run a firmware version, then control it like it's plugged in over USB.** You already
started the twin for Parts 4–5 with one command:

```bash
./qemu/studio.sh           # boots the twin, serves the Studio on :8080, AND muxes the console
```

For the firmware/attack parts you want the text console too. Open a second terminal and run:

```bash
./qemu/console.sh          # the plc> console — attaches to the bridge, so it runs AT THE SAME
                           # time as the browser Studio (no "stuck console", no separate run.sh)
```

You do **not** need a separate `./qemu/run.sh` for the default build — `studio.sh` already
booted it, and `console.sh` attaches to that same twin. (`run.sh` is only for booting a
*different* profile — `secure` or `malicious` — directly, as shown below.) The console is the
real USB console:
`mode run`, `io`, `force`, `status`, `busstream on`, `btn a|b|c`, `cwe`, `proto`,
`login`, `passwd`, `clk`, `ntpsync` all behave exactly as on a physical StampLC.

**Compare the two builds.** Part 9 hardens the firmware; build and boot the secure image
next to the vulnerable one and re-run the attacks:

```bash
./qemu/build-twin.sh secure    # compile + package the hardened image (once)
./qemu/run.sh secure           # boot it; ./qemu/console.sh to drive it
./remediation/verify-secure.sh # re-run the Part 6-8 attacks — each is now refused
```

`./qemu/run.sh malicious` boots the Part-7 supply-chain image the same way.

Under the hood it's a **patched** build of Espressif's QEMU (a stock `qemu-system-xtensa`
boots the chip but can't model the StampLC's I²C sensors, LCD, or W5500). The
prebuilt model avoids a local emulator build.
**[docs/QEMU-Twin.md](docs/QEMU-Twin.md)** records its observed interfaces, the
separate new-artifact physics gate, and the missing custom patch-source provenance.

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
