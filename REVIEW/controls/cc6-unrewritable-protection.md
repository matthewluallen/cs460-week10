# Control card — CC6 · Un-rewritable protection layer (the terminal barrier)

- **Risk / chain:** the final consequence of **every** chain — RELAY 1 drives the motor against intent or
  the safety interlock (signal-journey link 6). This is the barrier of last resort: what stops the motor
  if the logic, the firmware, or the network is fully owned.
- **Status:** draft.

## Primary mitigation
- **Control:** a protection the **control logic cannot modify** — a hardwired interlock or an **independent
  Safety Instrumented System (SIS)** separate from the Basic Process Control System, and/or a **mechanical
  protection layer**. · **Design (and physical).**
- **Where in the lab:** **deliberately absent.** Part 1 teaches that the snap-kit E-stop is **only an input
  to the logic** (not safety-rated, not independent), so a controller running someone else's code can
  ignore it — the exact gap TRITON exploited. Part 9 closes with the residual-risk sentence.
- **D3FEND:** out-of-band / physical (no software countermeasure) · **62443:** FR3 SI; the **SIS**
  concept (safety functions independent of the BPCS) · **NIST:** 800-82 safety-system overlay, **SI-17
  (fail-safe procedures)**, PE-* (physical) · **CIE:** *protection must not be something the control logic
  can rewrite; design the worst-case physical outcome to be bounded by a layer the attacker can't reach.*

## Compensating / defense-in-depth controls
| Control | Prevent or detect? | 62443 / NIST | What it still trusts |
|---|---|---|---|
| Hardwired E-stop that drops load power in hardware | prevent | SIS / SI-17 | the wiring, not the logic |
| Independent SIS (separate controller + sensors) | prevent | FR3 / 800-82 | the SIS's own integrity (TRITON targeted *this*) |
| Mechanical protection (relief, limit switch, fuse) | prevent | M0805 / PE | physics |
| Out-of-band monitoring / alarm | detect | SI-4 | an operator watching |

## Single points of failure & residual risk
- **SPOF:** in the lab's hardware there is **no un-rewritable protection** — the E-stop is a logic input and
  the relay is the only link to the motor's power. This is **by design, as the teaching point.**
- **Residual-risk sentence (the course's capstone):** "Even if the firmware is fully owned, RELAY 1 stays
  bounded by ___." In the lab today the honest answer is **nothing in software** — only a hardwired/independent
  layer the lab does *not* provide would bound it. TRITON's lesson: even the SIS can be a target.

## Does the lab teach AND assess both?
- **Taught:** Part 1 (the E-stop-is-only-an-input lesson) + Part 9 (the residual sentence). The *absence*
  is the lesson, which is pedagogically correct — but it should be **named as a deliberate design stance**,
  not read as an unfilled gap.
- **Assessed:** the residual-risk sentence is the capstone prompt (graded prose).

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| CC6-a | The deliberate absence of an un-rewritable protection should be stated explicitly as the design stance (so it isn't mistaken for a gap), and tied to 62443 SIS + CIE. | G7 | add one framing paragraph to Part 9 | P2 |
| CC6-b | No worked example of what a bounding layer *would* be (hardwired drop, independent SIS) — the capstone asks students to name one with no reference model. | G1 | add a reference "what would bound it" sidebar | P3 |
