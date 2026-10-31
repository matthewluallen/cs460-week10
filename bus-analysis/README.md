# Part 3 — Bus analysis: follow a bus exchange into the controller

Your task is to explain what a recorded exchange establishes, what remains
unknown, and which observation would distinguish a competing explanation. Keep
one finding in your course notebook or a local text file. Include your source,
reasoning, uncertainty and individual contribution or assistance. A successful
decoder run does not award a grade.

These are retained records from owned physical devices, not a live connection.
There are two different runs: September 12 serial exchanges and September 19
software-hook diagnostics. Do not combine their timestamps or firmware identities.
Nothing in the CLI connects to a device or the network — it runs entirely in your Codespace.

> **New here? Read this first.** Part 3 is one task: decode a recorded Modbus exchange with a
> single command and write a short finding about what it does — and does *not* — prove. That is
> the **required core** (the next three sections). The decoder needs no device, network, or
> setup. Everything under **Going deeper** further down is optional: it shows the same bus other
> ways (a logic-analyzer waveform, a live capture, the virtual tap). Do none, some, or all of it
> — skipping all of it still completes Part 3.

## Start with the evidence

Open `serial/exchanges.csv` in a text editor or spreadsheet. It lists exact
request/reply bytes, transaction identifiers and host receipt intervals. Compare
the two complete exchanges. `serial/timeout-events.csv` is an earlier, separate
run whose installed image was not established. An absent reply is not a zero.

Read `source/firmware-path.cpp.txt` beside the bytes. Its original numbered lines
connect input acquisition, program execution, output writes and register
publication. `source/serial-source.json` identifies that run's application and
source revisions. `source/installed-program.ir.json` is the retained instruction
representation, not a program to install for this activity.

## The decoder CLI (pure Node, no device)

Node.js is already installed in the Codespace — no packages, network, controller
or Python needed. From the repo root, run the decoder script — it decodes the
request, both complete replies, and the empty timeout reply in one pass:

```sh
./bus-analysis/decode.sh   # decode the RTU request + two replies + the timeout reply
```

The request should identify a valid function-03 read of register 25. Both
complete replies should report `status: valid` and a decoded `values`. The empty
timeout reply should report `unavailable`, with no accepted value and a nonzero
process exit — that is expected evidence handling, not a broken installation.

The two complete exchanges answer the **identical** request `01 03 00 19 00 01
55 cd` yet return different values (register 25 = 96 in physical-0, 99 in
physical-3, both CRC-valid): a reread is a *new acquisition* of a live quantity,
not corruption of the first reply.

Inspect the field explanations and compare the value with its source meaning.
The interactive analyzer below shows the same fields beside the bytes. The CLI's
other protocol modules (`tools/*.js`) are unchanged; this part's cases concern
Modbus RTU.

### Active learning
- **Predict** — before running, write down (a) what you think register 25
  decodes to and what it *means* (value, scale, signedness — note you cannot know
  scale without the firmware map), and (b) *why* two rereads of the identical
  request would differ, if they do.
- **Run** — `./bus-analysis/decode.sh`
- **Reconcile** — compare your prediction with the decoded `values` (96 vs 99).
  This establishes that both replies are well-formed, matched and CRC-valid, and
  that the difference is a fresh sample. It does **not** establish the physical
  quantity, its units, or that either number is independent process truth — a CRC
  authenticates nothing and a map is still required.
- **Pair check** — explain to a partner why "same bytes in, different value out"
  is evidence of acquisition timing, not of a corrupted or spoofed frame.

RS-485 is the electrical interface. Modbus RTU is the message format. A holding
register is a 16-bit value whose meaning comes from the device map. CRC means
cyclic redundancy check: it detects many byte errors but does not authenticate a
sender or establish freshness. In this format, address/data words put the high
byte first; the CRC puts the low byte first. The files are decoded serial bytes,
not a PCAP or sampled logic-analyzer channels. Do not invent an Ethernet layer.

## Finish the core — record your finding

Record one supported interpretation, an alternative cause, and the next useful
measurement: endpoints, signal/reference limits, instrument, common timing marker,
uncertainty and restoration. Extra downloads and time spent viewing do not establish mastery.
Keep the original files unchanged and write notes or generated output elsewhere.
Internet references are optional:
[Modbus application specification](https://www.modbus.org/file/secure/modbusprotocolspecification.pdf)
and [serial guide](https://www.modbus.org/file/secure/modbusoverserial.pdf).

**That is the required core of Part 3.** With your finding written, you're done.

---

## Going deeper (optional)

Everything below is **optional depth** — the same bus exchange seen other ways (a
logic-analyzer waveform, a live capture from the twin, the one-command virtual tap, and the
model finding exhibit). Pick any that interest you; skip them all and you have still completed
Part 3. None of it is graded beyond the finding you already recorded.

### Logic Studio — ladder authoring + bus monitor

The **TBD Logic Studio** ships in this repo at `../web/public/logic_studio.html`:
a ladder / Structured-Text editor with a **Bus monitor** whose lane-timeline view
is the same one `i2c-analyzer.html` uses. Open it in the Codespace by serving the
`web/public` folder (shared helper used by several parts) and forwarding the port:

```bash
./tools/serve-web.sh            # serves web/public on port 8000 (PORT=… to change); open logic_studio.html on the forwarded port
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

### A sigrok-style I²C view of the capture

`tools/bus_to_vcd.py` reconstructs a protocol-faithful **I²C logic-analyzer
capture (VCD)** from the `[BUS]` diagnostic records, so you can open the bus in
**PulseView** (or decode it headless with `sigrok-cli`). The waveform is
*reconstructed* from the decoded transactions at a synthetic I²C clock — a
protocol-timing view, not an analog acquisition (QEMU models the bus at the
transaction level).

```bash
./bus-analysis/to-sigrok.sh   # bus_to_vcd.py -> bus.vcd, then sigrok-cli decodes it (or open bus.vcd in PulseView)
```

Each transaction decodes back to its device and value — e.g. address `0x48`
(LM75) → data `0x0134` = 308 = 30.8 °C, matching the log. Open **`i2c-analyzer.html`** for the interactive view: each transaction decoded
on its device lane, stepped per ladder scan, with the input→logic→output
correlation and a freeze/force control that mirrors the twin's QMP register injection.

### Live tap of YOUR running twin → PulseView (the core path)

With your twin running (`./qemu/studio.sh`), record its I²C bus while you act:

```bash
./bus-analysis/tap.sh 10          # 10 s on i2c0 — press Start IN1 (di0) during it
# or: the "● Record twin bus (6 s)" button in the Studio's Bus monitor
```

The Studio server switches QEMU's own I²C-controller trace (`esp32_i2c_observe`) on through
QMP for those seconds, then off again, and converts it with `tools/trace_to_sigrok.py`. It is
the **same twin** your Studio, console and Modbus talk to — nothing is replayed or generated.
`tap.sh` prints the sigrok-cli decode for address `0x59` (each register value change, with its
time) and saves in `bus-analysis/out/`: `<time>.sr` (+ `<time>.pvs`, which stacks the I²C
decoder when PulseView opens the `.sr`), `.vcd`, `.decode.txt`, `.json`, `.manifest.json` and
a `.studio.json` for the Studio's Bus monitor. Inputs you change through `/field` during the
capture appear as channels `IN1`–`IN4`.

What is observed vs drawn: **bytes and their order are observed** (the emulated controller,
outside the firmware, so a malicious image cannot hide them); **edges are drawn** at 400 kHz
on the twin's virtual-time axis; the master's ACK/NACK on read bytes is inferred. Not shown:
analog detail, clock stretching, probes of absent addresses. For timing questions run the twin
real-time-aligned: `RESTART=1 ICOUNT=shift=5,sleep=on ./qemu/studio.sh`.

The teaching firmware also exposes this same bus traffic directly: `busstream on`
on the serial console streams decoded `@BUS <ms> <R|W> 0x<addr> <decode>` lines —
the live form of the exact transactions these recorded captures decode — and
`busstream off` stops it. Injecting a panel button with `btn a|b|c` is a clean way
to provoke a panel-expander transaction and watch the corresponding `@BUS` line
appear.

`-icount shift=N,sleep=on` runs the guest in wall-clock-aligned slow motion
(higher N = slower); `QMP stop`/`cont` freezes it outright. That is what keeps a
live SDA/SCL view legible, and it pairs with register injection (`qom-set
/machine/soc/aw9523 di=…` while stopped) to stage an input and watch the next
scan react.

### Parity: the virtual tap == a physical tap (same sigrok, same answers)

Remote students have no bench, so the emulated bus must decode **identically** to a
physical logic-analyzer tap. A logic analyzer is a *digital* sampler (logic
thresholds, not analog shape), and the SDA/SCL edges are fully determined by the
protocol + clock — so a faithful virtual tap and a physical probe yield the same
digital capture → the same sigrok decode.

`tools/trace_to_sigrok.py` is that virtual tap. It reads the QEMU I²C controller's
own `esp32_i2c_observe` trace — the complete byte-level bus traffic
(START / address / repeated-start / data / ACK-NACK / STOP, with `virtual_ns`
timing) — and reconstructs the SDA/SCL into a VCD. You run it through `./bus-analysis/tap.sh` (your running twin), not by invoking QEMU and the
reconstructor by hand.

Validated: it recovers register-pointer writes, data reads, repeated starts, and
the firmware's board-probe **NACKs** — exactly what a bench analyzer sees. The
same `sigrok-cli` / PulseView I²C decoder and the same analyzer view consume either
source. Bench students clip on a cheap sigrok-supported LA (fx2lafw / Pico) at the
same bus; the capture format and the answers match. Analog-only effects (rise
times, noise) live below the logic-analyzer abstraction and are the one thing a
scope — not this — would add, bench-only.

You don't drive QEMU or `trace_to_sigrok.py` by hand: `./bus-analysis/tap.sh` asks the Studio
server to trace your running twin and runs the reconstruction for you.

### CI check: a throwaway twin's idle bus (not the lab path)

`tools/twin_tap.sh` boots a separate copy of the twin with no console, so nobody can press
anything on it; CI (`qemu/smoke-test.py`) uses it to prove the released twin's bus decodes.
For the lab use `./bus-analysis/tap.sh` above.

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

### The chain in one page (the model finding exhibit)

`finding-exhibit.html` is the model deliverable: one weakness traced the whole way —
the SBOM library CVE (CWE‑295 / CWE‑522) → the CWE class (CWE‑319 cleartext +
CWE‑306 missing auth) → the vulnerable firmware code (`modbus_tcp_server.cpp`) → the
**captured Modbus frame** (the live FC05 write‑coil bytes, in plaintext, no auth) →
the physical outcome (relay0 actuates). It's what a student's finding should look
like: scanner question → analyzer answer, with evidence at each step.
