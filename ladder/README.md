# Part 2 — Ladder logic (the prebuilt block)

A PLC runs a tiny program on a loop: read inputs → evaluate logic → write outputs. You
won't write one from scratch. You start from a **complete, working program** and make
**one** change, so the focus stays on *what the logic does to the physical world*.

Two programs are provided:

- [`passthrough.st`](passthrough.st) — the simplest: **DI 0 → Relay 0**. One input, one
  output.
- [`latch_with_estop.st`](latch_with_estop.st) — a step up: the **factory motor program**
  (motor start/stop with a seal-in, an **N.C. E-stop** whose trip latches a fault, a Reset,
  and no automatic restart), written as readable structured text. The same program is the
  Studio example *Motor start/stop + E-stop* and is what the factory and twin images ship
  installed. Note the comment in it (`CWE-306`): the E-stop is only as trustworthy as the
  input image — anyone who can force or write it can make a tripped E-stop look healthy (we
  come back to this as a security point, and it's the thing to name in your finding).

**Output syntax.** `latch_with_estop.st` uses IEC located variables (`%IX0.n` = IN n+1,
`%QX0.n` = RELAY n+1), exactly like the Studio's ST export. `passthrough.st` writes its
output as **`COIL[n]`** (the Modbus coil image) — e.g. `COIL[0] := lamp;`. `COIL[0]` is the coil that energizes **physical relay0**,
which is what drives your lamp; they are the same output, not two different commands. (An
older draft of `passthrough.st` wrote `Relay[0]`; that has been normalized to `COIL[0]` so
the two samples agree and so the write lines up with the Modbus-coil security point above.)

## Your task — pick ONE

**(a) Toggle a rung.** In `passthrough.st`, switch from direct pass-through to *inverted*
by enabling RUNG 2 and disabling RUNG 1 (it's one line each, marked in the file). Predict
what the lamp will do before you run it.

**or (b) Add one simple function.** Give `passthrough.st` a second input so the output
needs **both**: an AND gate.

```
VAR  enable_sw : BOOL;  END_VAR     (* DI 1 *)
enable_sw := DI[1];
lamp := push_button AND enable_sw;  (* output only when BOTH are on *)
```

## How it gets onto the device

You don't flash over USB. In **Part 1** you open the class **front end**, pick your
program, and **Apply** — it loads onto your team's StampLC over the network. Then you
toggle the input and watch the output. The editor shows the same program as ladder and
as ST, so you can see the rung you changed either way.

## See it as a ladder — Logic Studio

The repo ships a browser ladder / Structured-Text editor, **TBD Logic Studio**, at
`web/public/logic_studio.html`. Open it to author — or just *see* — your change as a
ladder diagram and as ST side by side, and compile it to the device IR. Serve the browser
tools and open the forwarded port:

```bash
./tools/serve-web.sh   # serves web/public on :8000; open logic_studio.html (Ports tab in Codespaces)
```

Logic Studio now opens on a **command-center** layout with two live panels across the top:

- **Controller · panel** — a StampLC replica with the on-device **LCD** and the **A / B / C**
  keys. The keys drive that LCD (sending `btn a|b|c` when a real device is connected over
  USB, emulated otherwise) — the same screen and buttons you'd press on the bench.
- **Live signal** — a bus scope fed by the firmware's `busstream` (`@BUS …` lines; Studio
  turns it on with `busstream on` once connected). Each relay/coil write shows up as a mark
  on the *writes* lane, and the panel's relay glance flips with it.

That **Live signal** scope is the same bus you decoded in Part 3's
[`i2c-analyzer.html`](../bus-analysis/i2c-analyzer.html): when a rung drives the output, you
see the `0x59` AW9523 relay write cross the bus here. The ladder editor and the Part-3 bus
analyzer are **two views of one device** — the logic you author and the wire it moves.
Authoring here is optional and offline; your change reaches your team's PLC in **Part 1**
through the class front end, not over USB.

<!-- SHOT: part 2 — Logic Studio command-center: Controller·panel LCD + Live-signal scope showing a coil write -->

### Active learning

- **Predict.** Before you compile, write down the **relay0 / `COIL[0]`** state your rung
  produces for *each* input combination. For the invert rung: DI0=0 →? and DI0=1 →?. For the
  AND gate: fill the 2-input truth table (DI0,DI1 = 00, 01, 10, 11 →?). Commit to it on paper.
- **Run.** `./tools/serve-web.sh` → open Logic Studio, author/compile the rung, and toggle the
  inputs (watch the ladder coil energize and the **Live signal** scope mark the write).
- **Reconcile.** Compare your predicted `COIL[0]` against what the live ladder shows and the
  `@BUS … W 0x59 AW9523 relay ON/OFF` event. This establishes your rung's truth table *on this
  device's output image*; it does **not** prove the LED is physically wired (that's Part 1) or
  that the coil write is authenticated (it isn't — `CWE-306`).
- **Pair check.** Explain to a partner why `COIL[0]` is the value it is for one input combo.

Keep your finding small: which change you made, what you **predicted**, what the device
**did**, whether they matched — and name the **unauthenticated-coil / E-stop** exposure
(`CWE-306`) the sample surfaces, since the same coil you just drove takes a write from
anyone who can reach it.
