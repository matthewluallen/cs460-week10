# Control card — CC5 · Authenticated time + plausibility

- **Risk / chain:** the controller trusts an unauthenticated side input — network time (trust-time: NTP
  spoof / Y2038) or a sensor claim (trust-sensor). Consequence: a time- or sensor-dependent decision is
  made on a false value; or the evidence trail is corrupted.
- **Status:** draft.

## Primary mitigation
- **Control (time):** **monotonic audit clock** (logs on `millis()`, not wall-clock — the implemented fix)
  + **64-bit epoch storage** (secure build) + a **plausibility bound on time jumps**. · **Control
  (sensor):** the **freshness + range + rate** gate (E3). · **Code + design.**
- **Where:** Part 8 (the wrap + the monotonic-log proof), E3 (the sensor gate).
- **D3FEND:** D3-MAN, time integrity · **62443:** FR3 SI, **FR6 TRE** (timely response depends on time) ·
  **NIST:** **SC-45 (time synchronization)**, SI-10, AU-8 (audit time stamps) · **CIE:** *a value can
  arrive on time, intact, from a known sender, and still be false.*

## Compensating / defense-in-depth controls
| Control | Prevent or detect? | D3FEND / 62443 | What it still trusts |
|---|---|---|---|
| Authenticated time (NTS / signed time) | prevent | D3-MAN / FR1 | the time authority |
| Redundant time source + quorum | detect | FR3 / SC-45 | source independence |
| Out-of-band sensor cross-check | detect | FR3 / SI-4 | the second source |

## Single points of failure & residual risk
- **SPOF:** the plausibility gate and the monotonic clock are **detect/limit, not prevent** — they bound
  damage but still trust a single source. No authenticated time exists on the device.
- **Residual:** "the evidence survives a clock spoof (monotonic), but any wall-clock-dependent *decision*
  is still only bounded by the plausibility check."

## Does the lab teach AND assess both?
- **Taught + anchored:** Part 8 `verify-lab.py P8` (the wrap + monotonic log). E3 gate graded (prose).
- **Gap (CT1):** the lab proves the wrap and the log fix but never shows a **time-gated control decision**
  being flipped — so the "time → logic" join (trust-time link 4) is asserted, not demonstrated.

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| CT1 | no time-gated control decision shown being poisoned | G3 | add a maintenance-window/time permissive that a bad clock flips | P2 |
| CS3 | the strongest sensor control (out-of-band cross-check) is conceptual only | G4 | optional cross-check demo | P2 |
| CT2 | add CWE-190/191 alongside CWE-197 for Y2038 | G2 | Stage 5 | P3 |
