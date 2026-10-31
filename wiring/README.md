# Part 5 — Wire & run: one input → your logic → one output

Your ladder logic (Part 4) drives one input → one output. **The canonical path is your own
twin**: boot it, drive the input, and read the output back — no bench, no team code, the same
`plc>` console and `io` readback you'd get over USB on real hardware. An instructor-run
**Team Console** (a shared classroom bench) is an optional alternative, documented further down.

> Students: run it on your own twin (next section). The in-room wiring and Team Console
> sections below are the instructor's reference for the optional shared rig — there so you
> understand what you're driving, not something you assemble.

## Run it on your own twin (canonical)

No bench and no team code — each student drives their own twin, which boots the real firmware:

```bash
./qemu/run.sh        # boot your twin (leave it running)
./qemu/console.sh    # the plc> console, in another shell
```

Apply your Part‑4 program, then assert the input and read the output back:

```
mode run
force di0 1          # assert DI0 (the input your rung reads); 'force di0 0' clears it
io                   # read the output image back
```

```
DI : 1 0 0 0 0 0 0 0
RELAY cmd/act : 1/1 0/0 0/0 0/0
```

`RELAY cmd/act : 1/1` for relay0 is your evidence — the logic **commanded** the coil on
(`cmd=1`) and the device reports it **actually** on (`act=1`). Prefer that readback over "the
light looks on." You can also drive it visually in **Logic Studio** (`./qemu/studio.sh`). The
machine-checkable anchor is `python assessment/verify-lab.py P5`, which forces the input and
asserts the relay on the running target — the **same check passes on a physical StampLC**.

---

## Optional: the Team Console (shared classroom bench)

The rest of this page is the **optional** in-class path: an instructor pre-wires one bench per
team and students drive it remotely through a team-gated front end. The input→logic→output
result is the same as the twin above; the difference is a shared physical rig instead of your
own twin.

### The signal chain

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

### In-room wiring (what the bench looks like)

- **Output:** StampLC **relay0** terminal → SnapCircuits LED (through its resistor/lamp
  block). When your logic sets relay0, the LED lights.
- **Input (in-person):** a SnapCircuits **switch** wired into the **DI 0** loop. Flip it →
  DI 0 asserts.
- **Input (remote):** no wiring — the front end's **Trigger** uses the firmware `force`
  command to assert/clear DI 0 directly in the I/O image. (This is why we dropped the earlier
  "add an AtomS3 to drive the input" idea: `force` needs no second device, no loop‑voltage
  supply, and no power for a remote student.[^atom])

[^atom]: **Not the same Atom you meet elsewhere.** The dropped idea was an AtomS3 wired to
    *inject the DI 0 input* for this part. It is **not** the Atom **sensor spoofer**
    (`bus-analysis/sensor-spoofer`, an AtomS3 Lite that impersonates an I²C sensor) you use in
    Part 1. That spoofer sits **out‑of‑band** on the sensor bus and is a separate exercise;
    here in Part 5 there is no Atom at all — the input is `force`d in software.

> **Relay names.** The four relays are **relay0–relay3**. Names like `motor` or `light_green`
> are *labels you can give a relay inside a PLC program* — they aren't the relay's name. Part 4
> drives **relay0**.

### Run it

1. Open the front end, enter your **team code**.
2. **Apply** your program from Part 4.
3. **Trigger** the input (the button sends `force di0`; in person, flip the switch) and watch
   **output** flip.
4. **Read the output back over Modbus.** Don't rely on "the light looks on." In the Team
   Console's **device console**, type `io`. The reply names relay0's commanded and actual
   state:

   ```
   DI : 1 0 0 0 0 0 0 0
   RELAY cmd/act : 1/1 0/0 0/0 0/0
   ```

   `RELAY cmd/act : 1/1` for relay0 is your evidence: the logic **commanded** the coil on
   (`cmd=1`) *and* the device reports the output **actually** on (`act=1`). Quote that line in
   your finding — it's a readback a remote student can cite without eyes on the bench. (`status`
   also shows `mode`, `scan`, and `forces`. Note: the **mock** console returns a fixed snapshot
   tagged `(MOCK)` and does **not** reflect your `force` — the live `cmd/act` above is the real
   device, reached once `DEVICE_ENDPOINTS` is wired.)
5. Try your Part‑4 change (invert, or the AND gate) and confirm the readback changed to match.

### Active learning

- **Predict.** Pick **one** input→logic→output case (e.g. "DI0=1 with the pass‑through rung",
  or one row of your AND gate). Write down, before you touch the console, what `io` will report
  for relay0 as `cmd/act` — e.g. `1/1`, or `0/0`.
- **Run.** In the Team Console: **Trigger** the input, then type `io`.
- **Reconcile.** Compare your predicted `RELAY cmd/act` for relay0 against the readback. A
  match establishes that *your rung's logic reached the real output image over the network*; it
  does **not** prove the SnapCircuits LED is wired correctly (in‑room check) — and note the
  readback came back the same whether or not you were the only one holding the code (see the
  security beat below).
- **Pair check.** Tell a partner which line of `io` is your evidence and what `cmd` vs `act`
  each mean.

<!-- SHOT: part 5 — Team Console after Trigger: Output·Relay 0 lit and the `io` readback showing RELAY cmd/act 1/1 -->

> **Serving the console locally.** In class you get a hosted URL + team code. If you want to
> run the front end yourself (dev/offline, mock mode), serve the browser tools with
> `./tools/serve-web.sh` and open `index.html` (the Team Console).

### One security beat to notice

The front end asks for a code; the **device itself does not**. The same Modbus coil / `force`
path your trigger uses has **no authentication** — which is the point of the E‑stop comment in
`latch_with_estop.st`. In your finding, say what would happen if someone reached that coil
*without* going through the front end.

> **Serialized per team, but no lock on `force`.** The gateway serializes requests **per team
> code** — your code selects *your* team's device endpoint (`DEVICE_ENDPOINTS[team]`), and a
> team's requests are handled one lane at a time, so teams don't collide with each other. The
> **`force` path has no per‑team lock**, though: two people sharing one team code can drive the
> same coil with nothing serializing *them*, and the last write wins. That's why the `io`
> readback above can reflect a teammate's `force`, not yours — the device authorizes the *coil
> write*, not the *operator*.
