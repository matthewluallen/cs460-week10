# Part 5 — Run it remotely

Your ladder logic (Part 4) drives one input → one output on a real StampLC. **The bench is
pre‑wired by your instructor** — you don't build anything. You operate it **remotely**
through the class front end (URL + your team code, handed out in class), and you never join
the lab network.

> Students: skip to **"Run it"** below. The wiring section is the instructor's bench‑setup
> reference for how the pre‑wired rig is put together — it's here so you understand what
> you're driving, not something you assemble.

## The signal chain

```
   YOU (remote)                          LAB BENCH (your team's StampLC)
   ┌───────────┐   team code    ┌──────────┐      ┌────────────────────────────────┐
   │  Front end │ ────────────▶ │  Gateway  │ ──▶  │  force di0 ──▶ ladder logic     │  input
   │ (Netlify) │                │ (Funnel)  │      │   (Part 4)        │             │
   │  • trigger │ ◀──────────── │           │ ◀──  │                   ▼             │
   │  • apply   │   readback     └──────────┘      │              relay0 ──▶ LED     │  output
   │  • output? │                                  └────────────────────────────────┘
   └───────────┘
```

- **Input** — your logic's **DI 0**. In class it's a **SnapCircuits switch** wired into the
  DI 0 loop. For remote students the front end's **Trigger** button sends a `force di0 1`
  (and `force di0 0`) to the firmware, which drives DI 0 **in software** — a real input event
  in the I/O image, from anywhere, with no extra hardware to power or ship.
- **Output** — **relay0** switches a **SnapCircuits LED**. You also read the relay's state
  back over Modbus, so the front end can show "output: on/off" even without eyes on the bench.

## In-room wiring (what the bench looks like)

- **Output:** StampLC **relay0** terminal → SnapCircuits LED (through its resistor/lamp
  block). When your logic sets relay0, the LED lights.
- **Input (in-person):** a SnapCircuits **switch** wired into the **DI 0** loop. Flip it →
  DI 0 asserts.
- **Input (remote):** no wiring — the front end's **Trigger** uses the firmware `force`
  command to assert/clear DI 0 directly in the I/O image. (This is why we dropped the earlier
  "add an AtomS3 to drive the input" idea: `force` needs no second device, no loop‑voltage
  supply, and no power for a remote student.)

> **Relay names.** The four relays are **relay0–relay3**. Names like `motor` or `light_green`
> are *labels you can give a relay inside a PLC program* — they aren't the relay's name. Part 4
> drives **relay0**.

## Run it

1. Open the front end, enter your **team code**.
2. **Apply** your program from Part 4.
3. **Trigger** the input (the button sends `force di0`; in person, flip the switch) and watch
   **output** flip.
4. Try your Part‑4 change (invert, or the AND gate) and confirm the behavior changed.

## One security beat to notice

The front end asks for a code; the **device itself does not**. The same Modbus coil / `force`
path your trigger uses has **no authentication** — which is the point of the E‑stop comment in
`latch_with_estop.st`. In your finding, say what would happen if someone reached that coil
*without* going through the front end.
