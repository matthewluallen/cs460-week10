# Chain card — Trust chain: network time → logic (NTP / Y2038)

> Stage 3, chain #4. Merges into the Signal Journey at **link 3** (logic — any time-dependent decision).
> Part 8. Firmware: `time_sync.cpp`.

- **Consequence:** the controller trusts an unauthenticated time source; a spoofed date corrupts its clock
  (and anything that decides on wall-clock time).
- **Status:** draft.

## Links
| # | From → To | Lens | Establishes | Does NOT establish | Technique | CWE | Obs? |
|---|---|---|---|---|---|---|---|
| 1 | attacker is the time server the PLC is pointed at | Wire | an on-path position to the clock | that a real attacker **has** that position (you pointed it there) | **T0830** Adversary-in-the-Middle | — | yes (rogue NTP) |
| 2 | sends 2038-06-03 (64-bit, intact on the wire) | Wire | the date arrived uncorrupted | nothing about truth — it's just a claim | T0830 | — | yes |
| 3 | 64→32 truncation stores 1902-04-27 | Code · `time_sync.cpp` | the device **mis-stores** it (the wrap) | that the attacker caused a *decision* — needs a time-dependent rung | **CWE-197** (also 190/191) | yes (`plc> clk` → 1902) |
| 4 | the audit log stays monotonic (millis since boot) | Code | logs were **not** corrupted by the wrap (the fix rationale) | that *all* time uses are safe — only the audit path is monotonic | — | — | yes (`log` order held) |

## Bowtie — barriers
- **Authenticated time** (NTS / signed time) — vulnerable: none; the lab teaches the gap.
- **Plausibility bound on time jumps** (CIE: reject an implausible delta) + **64-bit storage** (secure
  build) — the implemented/compensating fixes.
- **Monotonic clock for audit** (D3-OM operation-monitoring analog) — the demonstrated fix: decouple the
  evidence trail from wall-clock.

## Residual risk
"Even if the clock is spoofed, RELAY 1 / the evidence stays bounded by ___" — the audit log (monotonic);
but **which control decision actually trusts wall-clock time?** The lab proves the wrap and the log fix but
does **not** show a control *action* that a bad clock changes — see gap T1.

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| B13 | No Threat-Map row (T0830 + CWE-197). | G8 | Stage 5: add it. | P1 |
| T1 | The chain merges at **link 3 (logic)** but the lab never shows a **time-gated control decision** being flipped — only the clock-wrap and the log fix. The "→ logic" join is asserted, not demonstrated. | G3 | add a small time-gated interlock (e.g. a maintenance-window permissive) so the poison reaches a relay. | P2 |
| T2 | Y2038 tagged CWE-197 (truncation) — add **CWE-190/191** (the canonical signed-overflow class) as the board noted. | G2 | Stage 5 | P3 |
