# Bench tap kit — the physical half of the parity pair

The emulated bus and a physical tap must decode to the **same answers** (see the
"Parity" section in `README.md`). This is the on‑bench side: what to clip onto a
real StampLC so an in‑person student runs the *same* sigrok flow and gets the *same*
transactions a remote student gets from the QEMU virtual tap.

## What a student runs is identical either way

| | Remote (no hardware) | Bench (hardware) |
|---|---|---|
| Capture source | `trace_to_sigrok.py` on the twin's `esp32_i2c_observe` trace | logic analyzer on the real I²C lines |
| Capture format | VCD (SDA, SCL) | sigrok `.sr` / VCD (SDA, SCL) |
| Decode | `sigrok-cli -P i2c:scl=SCL:sda=SDA` / PulseView I²C decoder | **same** |
| Correlation view | `i2c-analyzer.html` | **same** |
| Parity check | `parity_test.py` (virtual vs model ground truth) | `parity_test.py --physical bench.vcd` |

## The analyzer

Any **sigrok‑supported** logic analyzer works; the bus is 400 kHz I²C, so even the
cheapest is plenty.

- **Recommended (cheapest, ubiquitous):** an FX2‑based "24 MHz 8‑channel USB logic
  analyzer" (~$8–12), driven by sigrok's `fx2lafw`. 24 MHz sampling against a
  400 kHz bus is ~60× oversampling — clean edges.
- **Alternative:** a Raspberry Pi Pico running the sigrok Pico logic‑analyzer
  firmware (`raspberrypi-pico` driver). Good when you want a programmable trigger.
- **Premium:** a Saleae — same sigrok decoders, nicer UI. Not necessary.

Two channels are enough: **D0 → SDA**, **D1 → SCL**, plus a common **GND**. Keep the
decoder channel names `SDA`/`SCL` so the exact `sigrok-cli` line above works on both
sources.

## Where to probe

The StampLC runs **two** I²C buses (confirmed from the twin's trace):

- **`i2c0` — the base bus:** AW9523 relays + opto DI `0x59`, PI4IOE buttons `0x43`, INA226 `0x40`,
  LM75 `0x48`, RX8130 RTC `0x32`. This is the lab's bus (inputs → logic → relays).
- **`i2c1`:** the Grove / BME680 `0x77` path.

The base bus is internal, so the probe points come out on the **bottom expansion
header** (the connector the IO‑expansion module mates with) — SDA, SCL, and GND.
The Grove port exposes the `i2c1` bus directly.

> **Instructor pre‑wire.** Confirm the exact SDA/SCL/GND pins on the expansion
> header against the StampLC schematic and bring them to a 3‑pin breakout before
> class (consistent with the rest of the pre‑wired bench). Students clip the two
> signals + ground and capture; they don't open the unit. On 3.3 V logic the FX2
> thresholds are fine; add series 100 Ω if cable length causes ringing.

## Capture settings

- Channels: `D0=SDA`, `D1=SCL` (rename to `SDA`,`SCL` in PulseView, or map with
  `-C` in `sigrok-cli`).
- Sample rate: **4 MHz is plenty** (≥10× the 400 kHz bus); 24 MHz if you want margin.
- Trigger: optional — a falling edge on SDA while SCL is high catches a START.
- Add the **I²C** protocol decoder; set its `scl`/`sda` channels.

```bash
# capture a couple of scans' worth, then decode the same way as the twin
sigrok-cli -d fx2lafw --config samplerate=4m -C SDA,SCL --samples 2M -o bench.sr
sigrok-cli -i bench.sr -P i2c:scl=SCL:sda=SDA -A i2c            # same answers as the twin
```

## Prove parity on the day

Capture the same scenario both ways (e.g., force DI0 and watch relay0), then:

```bash
# virtual (from the twin trace) vs the controller-model ground truth
python3 tools/parity_test.py trace.log --bus i2c0
# and virtual vs the physical capture
python3 tools/parity_test.py trace.log --bus i2c0 --physical bench.vcd
```

Matching decoded transactions is the guarantee that the remote and in‑person labs
grade the same. The only thing the bench adds and the twin can't is **analog**
behaviour — rise times, bus capacitance, noise, true glitches — which lives *below*
the logic‑analyzer abstraction (a digital LA doesn't capture it either). If a lesson
ever needs that, it's an oscilloscope, and it's inherently bench‑only.
