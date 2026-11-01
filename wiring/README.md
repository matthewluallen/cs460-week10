# Part 1 — Wire & run: one input → your logic → one output

Your ladder logic (Part 2) drives one input → one output. **The canonical path is your own
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

### The factory program: motor start/stop + E-stop (fail-safe)

The twin image (and the factory image you would flash to a real board) ships with one program
already installed: **Motor start/stop + E-stop**
([`web/public/projects/motor-start-stop-estop.csco`](../web/public/projects/motor-start-stop-estop.csco),
open it in the Logic Studio under *Example projects*). It boots in **PROGRAM**; you choose when
to RUN. The names below are the program's tags — they are what `force <tag>` accepts.

| Board terminal (firmware) | Tag | Device | Meaning of 1 |
|---|---|---|---|
| IN1 (di0) | `start_pb` | Start push button, N.O. momentary | pressed |
| IN2 (di1) | `stop_pb` | Stop push button, N.O. momentary | pressed |
| IN3 (di2) | `estop_ok` | E-stop slide switch wired **N.C.** | **healthy** (0 = tripped *or wire cut*) |
| IN4 (di3) | `reset_pb` | Fault reset push button, N.O. momentary | pressed |
| RELAY 1 (relay0) N.O. | `motor` | motor (through a snap-kit relay on its own battery) | running |
| RELAY 2 (relay1) N.O. | `run_light` | green run light | follows the motor |
| RELAY 4 (relay3) N.O. | `fault_lamp` | red fault lamp — it *is* the latched E-stop fault | fault latched |

What it does:

- A **new** Start press seals the motor in; releasing Start keeps it running. Stop drops it.
- `estop_ok` = 0 (E-stop pressed, wire cut, loop supply lost) drops the motor **and latches the
  fault**. Restoring `estop_ok` = 1 does **not** restart the motor and does **not** clear the fault.
- Reset clears the fault only while `estop_ok` = 1, and the motor stays off. Only a new Start
  press runs it again. A held or stuck Start or Reset button does nothing (both act on a press).
- **Stop is N.O.** because the kit has no N.C. push button: a cut Stop wire cannot stop the
  motor. The N.C. E-stop is the input that fails safe.

**The twin's inputs all read 0 at boot**, so the E-stop reads **tripped** until something drives
IN3 to 1 — exactly what a real N.C. loop shows with nothing wired. Hold it healthy first:

```
plc> force estop_ok 1      # E-stop slide ON (healthy): drives IN3 = 1
plc> mode run
plc> force reset_pb 1      # clears a fault that latched while IN3 read 0 (if the lamp is lit)
plc> force reset_pb 0
plc> force start_pb 1      # press Start ...
plc> force start_pb 0      # ... and release it: the motor stays on (seal-in)
plc> io
```

Expected (`F` marks a forced input):

```
DI  : 0F 0F 1F 0F 0 0 0 0
relay commanded/actual (base 0-3):
  relay0 cmd=1 act=1        motor
  relay1 cmd=1 act=1        run light
  relay2 cmd=0 act=0
  relay3 cmd=0 act=0        fault lamp
```

Now trip it and recover it:

```
plc> force estop_ok 0      # trip: relay0 and relay1 drop, relay3 (fault lamp) latches on
plc> force estop_ok 1      # restore: nothing restarts and relay3 stays on
plc> force reset_pb 1      # reset: relay3 clears, relay0 stays OFF
plc> force reset_pb 0
plc> force start_pb 1      # only a new Start press runs the motor again
plc> force start_pb 0
plc> force stop_pb 1       # stop it
plc> unforce start_pb
plc> unforce stop_pb
plc> unforce reset_pb
plc> unforce estop_ok      # never leave a safety input forced; on the twin IN3 now reads 0 -> fault lamp
```

`relay0 cmd/act 1/1` is your evidence — the logic **commanded** the coil on (`cmd=1`) and the
device reports it **actually** on (`act=1`). Prefer that readback over "the light looks on." You
can also drive it visually in **Logic Studio** (`./qemu/studio.sh`). The machine-checkable anchor
is `python assessment/verify-lab.py P1`, which runs exactly this sequence (seal-in, stop, trip,
no restart, reset, start again) and asserts the relays — the **same check passes on a physical
TBD controller**.

> **Safety beat.** `force estop_ok 1` *defeats* the E-stop: the PLC believes the loop is
> healthy whatever the switch says. Real safety controllers refuse forces on safety I/O, and a
> real E-stop also breaks the motor supply directly (a second, hard-wired channel), so no
> program, force or unauthenticated coil write can keep the motor running.

**What a predesigned circuit must drive at start-up.** The E-stop slide switch starts **ON**
(IN3 = 1, `estop_ok` healthy) from the moment the circuit is placed, and the three push buttons
start released (IN1, IN2, IN4 = 0). If RUN began while IN3 still read 0, the fault lamp is lit:
press Reset once.

**Your own Part-2 program replaces it.** Loading a project writes it into your twin's flash, so it
survives a reboot of that image. To get the factory program back, open *Example projects → Motor
start/stop + E-stop* in the Logic Studio and Load it (or re-fetch the twin image).

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
   │  Front end │ ────────────▶ │  Gateway  │ ──▶  │  force IX0 ──▶ ladder logic     │  input
   │ (Netlify) │                │ (Funnel)  │      │   (Part 2)        │             │
   │  • trigger │ ◀──────────── │           │ ◀──  │                   ▼             │
   │  • apply   │   readback     └──────────┘      │              relay0 ──▶ LED     │  output
   │  • output? │                                  └────────────────────────────────┘
   └───────────┘
```

- **Input** — your logic's **IN1 (DI 0, `%IX0`)**. In class it's a **snap-kit switch** wired into
  the IN1 loop. For remote students the front end's **Trigger** button sends a `force IX0 1`
  (and `force IX0 0`) to the firmware, which drives IN1 **in software** — a real input event
  in the I/O image, from anywhere, with no extra hardware to power or ship.
- **Output** — **RELAY 1 (relay0)** switches a **snap-kit LED**. You also read the relay's state
  back over Modbus, so the front end can show "output: on/off" even without eyes on the bench.

### In-room wiring (what the bench looks like)

- **Output:** TBD **RELAY 1 (relay0)** N.O. terminal → snap-kit LED (through its
  resistor/lamp block). When your logic sets relay0, the LED lights.
- **Input (in-person):** a snap-kit **switch** wired into the **IN1 (DI 0)** loop. Flip it →
  DI 0 asserts.
- **Input (remote):** no wiring — the front end's **Trigger** uses the firmware `force`
  command to assert/clear DI 0 directly in the I/O image. (This is why we dropped the earlier
  "add an AtomS3 to drive the input" idea: `force` needs no second device, no loop‑voltage
  supply, and no power for a remote student.[^atom])

[^atom]: **Not the same Atom you meet elsewhere.** The dropped idea was an AtomS3 wired to
    *inject the DI 0 input* for this part. It is **not** the Atom **sensor spoofer**
    (`bus-analysis/sensor-spoofer`, an AtomS3 Lite that impersonates an I²C sensor) you use in
    Part 3. That spoofer sits **out‑of‑band** on the sensor bus and is a separate exercise;
    here in Part 1 there is no Atom at all — the input is `force`d in software.

> **Relay names.** The board prints **RELAY 1–4**; the firmware calls them **relay0–relay3**.
> Names like `motor` or `run_light` are *labels a PLC program gives a relay* (the factory program
> uses those) — they aren't the relay's name. Part 2 drives **relay0** (RELAY 1).

### Run it

1. Open the front end, enter your **team code**.
2. **Apply** your program from Part 2.
3. **Trigger** the input (the button sends `force IX0`; in person, flip the switch) and watch
   **output** flip.
4. **Read the output back over Modbus.** Don't rely on "the light looks on." In the Team
   Console's **device console**, type `io`. The reply names relay0's commanded and actual
   state:

   ```
   DI  : 1F 0 0 0 0 0 0 0
   relay commanded/actual (base 0-3):
     relay0 cmd=1 act=1
     relay1 cmd=0 act=0
     relay2 cmd=0 act=0
     relay3 cmd=0 act=0
   ```

   `relay0 cmd=1 act=1` is your evidence: the logic **commanded** the coil on
   (`cmd=1`) *and* the device reports the output **actually** on (`act=1`). Quote that line in
   your finding — it's a readback a remote student can cite without eyes on the bench. (`status`
   also shows `mode`, `scan`, and `forces`. Note: the **mock** console returns a fixed snapshot
   tagged `(MOCK)` and does **not** reflect your `force` — the live `cmd/act` above is the real
   device, reached once `DEVICE_ENDPOINTS` is wired.)
5. Try your Part‑2 change (invert, or the AND gate) and confirm the readback changed to match.

### Active learning

- **Predict.** Pick **one** input→logic→output case (e.g. "DI0=1 with the pass‑through rung",
  one row of your AND gate, or "E-stop restored after a trip" with the factory program). Write
  down, before you touch the console, what `io` will report for relay0 as `cmd/act` — e.g. `1/1`,
  or `0/0`.
- **Run.** In the Team Console: **Trigger** the input, then type `io`.
- **Reconcile.** Compare your predicted `RELAY cmd/act` for relay0 against the readback. A
  match establishes that *your rung's logic reached the real output image over the network*; it
  does **not** prove the snap-kit LED is wired correctly (in‑room check) — and note the
  readback came back the same whether or not you were the only one holding the code (see the
  security beat below).
- **Pair check.** Tell a partner which line of `io` is your evidence and what `cmd` vs `act`
  each mean.

<!-- SHOT: part 1 — Team Console after Trigger: Output·Relay 0 lit and the `io` readback showing RELAY cmd/act 1/1 -->

> **Serving the console locally.** In class you get a hosted URL + team code. If you want to
> run the front end yourself (dev/offline, mock mode), serve the browser tools with
> `./tools/serve-web.sh` and open `index.html` (the Team Console).

### One security beat to notice

The front end asks for a code; the **device itself does not**. The same Modbus coil / `force`
path your trigger uses has **no authentication** — which is the point of the E‑stop comment in
`latch_with_estop.st`, and why `force estop_ok 1` can hold a tripped E-stop "healthy". In your finding, say what would happen if someone reached that coil
*without* going through the front end.

> **Serialized per team, but no lock on `force`.** The gateway serializes requests **per team
> code** — your code selects *your* team's device endpoint (`DEVICE_ENDPOINTS[team]`), and a
> team's requests are handled one lane at a time, so teams don't collide with each other. The
> **`force` path has no per‑team lock**, though: two people sharing one team code can drive the
> same coil with nothing serializing *them*, and the last write wins. That's why the `io`
> readback above can reflect a teammate's `force`, not yours — the device authorizes the *coil
> write*, not the *operator*.
