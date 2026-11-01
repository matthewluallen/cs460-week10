# Control card — CC1 · Input integrity gate

- **Risk / chain:** a value the logic reads is forged — a spoofed field input (signal-journey link 2, the
  I²C/sensor-spoof) or a false phone/sensor claim (trust-sensor). Consequence: the logic acts on a lie.
- **Status:** draft.

## Primary mitigation
- **Control:** validate a value before the logic trusts it — **plausibility: in-range ∧ bounded rate ∧
  fresh**. · **Code or design?** code (a rung/gate) + design (what "plausible" means).
- **Where in the lab:** the **E3** phone-FDI assignment builds this for the sensor path and proves the real
  reading still passes. **Not** applied to the physical input path (di0) or the analog CH1 path.
- **D3FEND:** D3-MAN (message auth), input/semantic validation · **62443:** FR3 System Integrity, FR1 IAC
  (source identity) · **NIST:** SI-10 (information input validation), SC-8 (transmission integrity) ·
  **CIE:** *every value entering control logic is a claim, not a fact.*

## Compensating / defense-in-depth controls
| Control | Prevent or detect? | D3FEND / 62443 | What it still trusts |
|---|---|---|---|
| Per-source authentication / id pinning (`mqtt pin`) | prevent (weakly) | D3-MAN / FR1 | that the id isn't stolen |
| Freshness requirement (stale after 8 s) | detect (withholding) | FR6 TRE | the clock timing the freshness |
| Redundant / out-of-band cross-check sensor | detect (integrity) | FR3 / SI-4 | the second sensor's independence |
| Bus physical-access control (no I²C MITM) | prevent | 62443 FR5 / PE-3 | physical security of the bus |

## Single points of failure & residual risk
- **SPOF:** a plausibility gate is **detect/limit, not prevent** — a patient attacker who stays in-range,
  fresh, and bounded-rate still passes.
- **Residual:** "Even with the gate, the alarm still trusts a single source and the plausibility bounds."

## Does the lab teach AND assess both?
- **Taught:** sensor path (E3). **Assessed:** E3 is graded, but **prose-only — no machine anchor** (gap CS1).
- **Gap:** the physical/analog input path has **no** plausibility gate and the bus-spoof attack
  (sensor-spoofer) is shown without the corresponding defense.

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| CC1-a | Plausibility gate taught for the sensor path only; not for di/CH1 inputs | G4 | add a range/rate check example on a wired input | P2 |
| CS1 | E3 gate has no machine-checkable anchor | G5/G6 | a verifier submitting spoof + real | P1 |
