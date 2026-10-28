# Test your logic on the running controller — the Studio Test bench

You built a rung in Part 4. Now **drive it** on the running firmware with a simple
virtual circuit — a pushbutton and an LED — and watch your logic move a real output.
No hardware required: everything here runs against the **QEMU twin**, and every step
below was validated on a physical M5StampLC as well (the bench is the reference, not
something you need to own).

> Every readback and result in this section was captured live from the firmware
> console on both the twin and a real StampLC. If your result differs, that's a finding
> — write down what you observed and what you'd check next.

![The pushbutton → green LED circuit in the Studio Test bench](../docs/img/bench-01-circuit-pushbutton.png)

The wiring mirrors a real rig: the **+V** rail feeds the pushbutton and the relay
common (COM); pressing the button energizes **digital input 1 (DI1)**; your ladder
decides what happens; **Relay 1's N.O. contact** switches +V to the **green LED**, and
the LED returns to the **−** rail. The LED lights **only if your ladder connects DI1 to
Relay 1** — so this tests *your logic*, not a hard-wired shortcut.

## A. Build and load the one-rung program

1. In Logic Studio, build one rung: a **normally-open contact on `%IX1`** driving the
   **coil `%QX1`** (Relay 1). (`%IX1` is digital input 1; `%QX1` is Relay 1.)
2. Click **Load application**. Loading requires the controller to be in **PROGRAM**
   mode — the loader refuses a download while running:

   ```
   plc> mode prog
   mode PROGRAM (outputs inhibited, download enabled)
   plc> PLCLOAD …
   [LOADER] OK instr=3 crc=e7ac24c8 scan_ms=50
   [LOADER] applied
   ```
3. Set the controller to **RUN** (`mode run`), and **Start monitoring**.

## B. Test it — press the button

Open **⚡ Test bench → Pushbutton → Green LED** and **press and hold** the button.
Behind the scenes the bench asserts your input (`force IX1 1`); your rung runs; Relay 1
closes; the green LED lights.

![Button pressed — Relay 1 on, green LED lit](../docs/img/bench-02-pressed-green.png)

**Reconcile with the readback — don't trust the glow.** In the console, `io`:

```
plc> force IX1 1
forced %IX1 = 1
plc> io
DI  : 0 1F 0 0 0 0 0 0
relay commanded/actual (base 0-3):
  relay0 cmd=0 act=0
  relay1 cmd=1 act=1      ← your rung commanded Relay 1 on AND the device reports it on
  relay2 cmd=0 act=0
```

`relay1 cmd/act = 1/1` is your evidence: the logic **commanded** the coil (`cmd=1`)
*and* the device reports the output **actually** on (`act=1`). Relay 0 stays `0/0`,
proving it's *your* DI1→Relay 1 rung running — not some built-in behavior. Release the
button and `relay1` returns to `0/0`.

### Predict → Run → Reconcile

- **Predict.** Before you press: what will `io` report for `relay1` as `cmd/act`?
- **Run.** Press the button; type `io`.
- **Reconcile.** Did `relay1 cmd/act` match your prediction? A match establishes that
  your rung reached the real output image. The LED *looking* on does not — cite the
  readback line in your finding.

## C. Add logic — a second LED, normally-open vs normally-closed

Now extend the program and meet **NO (normally-open)** vs **NC (normally-closed)**
contacts. Add a **second rung**: a **normally-closed contact on `%IX1`** driving
**`%QX2`** (Relay 2). Load it (PROGRAM → Load → RUN) and open **Test bench → Two LEDs —
NO / NC**.

A NO contact passes power when its input is **true** (pressed); a NC contact passes
power when its input is **false** (released). So the two rungs do opposite things from
the *same* button.

| Button | `%IX1` | Rung 1: NO → Relay 1 (green) | Rung 2: NC → Relay 2 (amber) |
|--------|--------|------------------------------|------------------------------|
| released | 0 | off | **on** |
| pressed  | 1 | **on** | off |

![Released — amber (NC) on, green off](../docs/img/bench-04-nonc-released.png)
![Pressed — green (NO) on, amber off](../docs/img/bench-05-nonc-pressed.png)

Verified readbacks:

```
# released (IX1 = 0)
plc> io
  relay1 cmd=0 act=0        green (NO) off
  relay2 cmd=1 act=1        amber (NC) on

# pressed (force IX1 1)
plc> io
  relay1 cmd=1 act=1        green (NO) on
  relay2 cmd=0 act=0        amber (NC) off
```

### Predict → Run → Reconcile

- **Predict.** With the button **released**, which LED is on — and why? Now flip your
  rung-2 contact from NC to NO and predict again.
- **Run.** Load, RUN, press/release; read `io`.
- **Reconcile.** Explain, in terms of the contact type, which LED followed the button.
  Then: what single rung would make **both** LEDs turn on together?

## What you can rely on (and what you can't)

- Force the input by its **logical operand**: `force IX1 1` (the Test bench button does
  this for you). `force di1` is **not** a valid tag — the firmware forces `%IX`/`%QX`.
- You must be in **PROGRAM** mode to load a program; **RUN** to execute it.
- An LED only lights if your **ladder** drives its relay. The bench proves your *logic*,
  not your wiring — and `io`'s `cmd/act` is the readback a remote student can cite
  without eyes on the bench.
- **Security beat:** `force` and the Modbus coil write that your trigger rides on have
  **no authentication** (the same CWE-306 path as the E-stop coil). In your finding,
  say what would happen if someone reached that coil without going through the UI.
