# Part 1 — Bus analysis: follow a bus exchange into the controller

Your task is to explain what a recorded exchange establishes, what remains
unknown, and which observation would distinguish a competing explanation. Keep
one finding in your course notebook or a local text file. Include your source,
reasoning, uncertainty and individual contribution or assistance. A successful
decoder run does not award a grade.

These are retained records from owned physical devices, not a live connection.
There are two different runs: September 12 serial exchanges and September 19
software-hook diagnostics. Do not combine their timestamps or firmware identities.
Nothing in the CLI connects to a device or the network — it runs entirely in your Codespace.

## Start with the evidence

Open `serial/exchanges.csv` in a text editor or spreadsheet. It lists exact
request/reply bytes, transaction identifiers and host receipt intervals. Compare
the two complete exchanges. `serial/timeout-events.csv` is an earlier, separate
run whose installed image was not established. An absent reply is not a zero. Note the two
complete exchanges answer the *same* request (`01 03 00 19 00 01 55 cd`, register 25) with
**different** values (96 vs 99): a reread is a new measurement, not proof either value is
stable or "the" value — an observed-vs-inferred point, not corruption.

Read `source/firmware-path.cpp.txt` beside the bytes. Its original numbered lines
connect input acquisition, program execution, output writes and register
publication. `source/serial-source.json` identifies that run's application and
source revisions. `source/installed-program.ir.json` is the retained instruction
representation, not a program to install for this activity.

## The decoder CLI (pure Node, no device)

Node.js is already installed in the Codespace — no packages, network, controller
or Python needed. From the repo root, change into this folder, then run:

```sh
cd bus-analysis          # all paths below are relative to this folder
node tools/decode.js rtu-request serial/physical-0-request.bin
node tools/decode.js rtu-reply serial/physical-0-reply.bin serial/physical-0-request.bin
node tools/decode.js rtu-reply serial/physical-3-reply.bin serial/physical-3-request.bin
node tools/decode.js rtu-reply serial/timeout-reply.bin serial/timeout-request.bin
```

The first command should identify a valid function-03 request. Complete replies
should report `status: valid` and decoded `values`. The empty timeout reply
should report `unavailable`, with no accepted value and a nonzero process exit.
That last result is expected evidence handling, not a broken installation.
Inspect the field explanations and compare the value with its source meaning.
The interactive analyzer below shows the same fields beside the bytes. The CLI's
other protocol modules (`tools/*.js`) are unchanged; this part's cases concern
Modbus RTU.

RS-485 is the electrical interface. Modbus RTU is the message format. A holding
register is a 16-bit value whose meaning comes from the device map. CRC means
cyclic redundancy check: it detects many byte errors but does not authenticate a
sender or establish freshness. In this format, address/data words put the high
byte first; the CRC puts the low byte first. The files are decoded serial bytes,
not a PCAP or sampled logic-analyzer channels. Do not invent an Ethernet layer.

## Optional Logic Studio — ladder authoring + bus monitor

The **TBD Logic Studio** ships in this repo at `../web/public/logic_studio.html`:
a ladder / Structured-Text editor with a **Bus monitor** whose lane-timeline view
is the same one `i2c-analyzer.html` uses. Open it in the Codespace by serving the
`web/public` folder and forwarding the port:

```bash
python3 -m http.server -d web/public 8000   # from the repo root; open logic_studio.html on forwarded port 8000
```

In **Bus monitor → Open capture**, choose `bus-analysis/hooks/physical-bus-capture.json`.
For this recorded-file investigation, don't use Connect, Capture, RUN or Download
to controller.

Select record 90 and compare its temperature annotation with `hooks/readback.csv`.
Select record 83 and explain the missing-record indication. The old 0x43 opto
label is preserved: it is a known software label error, not discovery of an I2C
device at that address. A later firmware correction does not rewrite this record.
`source/capture-identity.json` retains selected boot, firmware and program fields
from before/after this capture; opening USB changed the boot in that run. The raw
capture's source note names the original repository source, which is not bundled
in full. Only the explicitly identified identity fields are projected here.

The hook's milliseconds, diagnostic enqueue microseconds and host receipt time
have different meanings. A readback and hook from the same controller are not
independent electrical measurements. A separate meter, sampled channels and clock
alignment are needed for an actual bus-to-load timing claim. Retained evidence
supports interpretation; it does not establish your physical probing competence.

## Finish or revisit

Record one supported interpretation, an alternative cause, and the next useful
measurement: endpoints, signal/reference limits, instrument, common timing marker,
uncertainty and restoration. Revisit a different interface later if transfer is
the objective. Extra downloads and time spent viewing do not establish mastery.

Keep the original files unchanged and write notes or generated output elsewhere.
Internet references are optional:
[Modbus application specification](https://www.modbus.org/file/secure/modbusprotocolspecification.pdf)
and [serial guide](https://www.modbus.org/file/secure/modbusoverserial.pdf).
`manifest.json` lists file sizes/hashes. Hashes detect corruption but do not
authenticate a source.

## Optional: a sigrok-style I²C view of the capture

`tools/bus_to_vcd.py` reconstructs a protocol-faithful **I²C logic-analyzer
capture (VCD)** from the `[BUS]` diagnostic records, so you can open the bus in
**PulseView** (or decode it headless with `sigrok-cli`). The waveform is
*reconstructed* from the decoded transactions at a synthetic I²C clock — a
protocol-timing view, not an analog acquisition (QEMU models the bus at the
transaction level).

```bash
python3 tools/bus_to_vcd.py hooks/physical-bus-capture.json bus.vcd
sigrok-cli -i bus.vcd -P i2c:scl=SCL:sda=SDA -A i2c   # Start/Address/Data/ACK/Stop
# or open bus.vcd in PulseView and add the I²C decoder (SCL, SDA)
```

Each transaction decodes back to its device and value — e.g. address `0x48`
(LM75) → data `0x0134` = 308 = 30.8 °C, matching the log. Open **`i2c-analyzer.html`** for the interactive view: each transaction decoded
on its device lane, stepped per ladder scan, with the input→logic→output
correlation and a freeze/force control that mirrors the twin's QMP register injection.

## Live sampling from the running twin (slow it down first)

`tools/twin_sampler.py` polls the firmware's `/api/points` over the emulated
W5500 stack and reconstructs the per-scan I²C transactions into a growing VCD +
JSON — a **live** capture while the logic runs. Run the twin **slowed** so the
bus cadence is human-paced instead of thousands of transactions a second:

```bash
# slowed twin:  qemu-system-xtensa ... -icount shift=5,sleep=on ...
python3 tools/twin_sampler.py --samples 20 --interval 0.5 --out live
sigrok-cli -i live.vcd -P i2c:scl=SCL:sda=SDA -A i2c
```

`-icount shift=N,sleep=on` runs the guest in wall-clock-aligned slow motion
(higher N = slower); `QMP stop`/`cont` freezes it outright. That is what keeps a
live SDA/SCL view legible, and it pairs with register injection (`qom-set
/machine/soc/aw9523 di=…` while stopped) to stage an input and watch the next
scan react.

## Parity: the virtual tap == a physical tap (same sigrok, same answers)

Remote students have no bench, so the emulated bus must decode **identically** to a
physical logic-analyzer tap. A logic analyzer is a *digital* sampler (logic
thresholds, not analog shape), and the SDA/SCL edges are fully determined by the
protocol + clock — so a faithful virtual tap and a physical probe yield the same
digital capture → the same sigrok decode.

`tools/trace_to_sigrok.py` is that virtual tap. It reads the QEMU I²C controller's
own `esp32_i2c_observe` trace — the complete byte-level bus traffic
(START / address / repeated-start / data / ACK-NACK / STOP, with `virtual_ns`
timing) — and reconstructs the SDA/SCL into a VCD:

```bash
qemu-system-xtensa ... --trace enable=esp32_i2c_observe -D trace.log
python3 tools/trace_to_sigrok.py trace.log --bus i2c0 --out bus
sigrok-cli -i bus.vcd -P i2c:scl=SCL:sda=SDA -A i2c    # decodes like a bench capture
```

Validated: it recovers register-pointer writes, data reads, repeated starts, and
the firmware's board-probe **NACKs** — exactly what a bench analyzer sees. The
same `sigrok-cli` / PulseView I²C decoder and the same analyzer view consume either
source. Bench students clip on a cheap sigrok-supported LA (fx2lafw / Pico) at the
same bus; the capture format and the answers match. Analog-only effects (rise
times, noise) live below the logic-analyzer abstraction and are the one thing a
scope — not this — would add, bench-only.

## One-command virtual tap → analyzer / PulseView

```bash
./qemu/fetch-qemu.sh                     # once
BUS=i2c0 RUN=5 ./bus-analysis/tools/twin_tap.sh
# wrote bus.vcd + bus.json
#   → open i2c-analyzer.html, click "Load capture", pick bus.json
#   → or: sigrok-cli -i bus.vcd -P i2c:scl=SCL:sda=SDA -A i2c
```

The analyzer's **Load capture** button renders a real controller-model (or
physical-tap) capture on the device lanes; the seeded scenario stays the default
correlation demo.

## The chain in one page (graded exhibit)

`finding-exhibit.html` is the model deliverable: one weakness traced the whole way —
the SBOM library CVE (CWE‑295 / CWE‑522) → the CWE class (CWE‑319 cleartext +
CWE‑306 missing auth) → the vulnerable firmware code (`modbus_tcp_server.cpp`) → the
**captured Modbus frame** (the live FC05 write‑coil bytes, in plaintext, no auth) →
the physical outcome (relay0 actuates). It's what a student's finding should look
like: scanner question → analyzer answer, with evidence at each step.
