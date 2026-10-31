# Control card — <RISK / CONTROL NAME>

> A risk and its full control story (Stage 4). Copy to `REVIEW/controls/<id>.md`.

- **ID / name:**
- **Risk (chain + consequence):** <link to Chain card + the worst outcome>
- **Owner / reviewer:** · **Status:** draft / reviewed / fixed / signed-off

## Primary mitigation
- **Fix:** <the direct fix (code or design)> · **Code or design?** <which>
- **Where in the lab:** <Part / verifier that proves it>
- **D3FEND:** · **62443 FR/SL:** · **NIST 800-53/800-82:** · **CIE principle:**

## Compensating / defense-in-depth controls
<the other barriers that reduce the risk even if the primary fails: zones/conduits, egress filtering,
an internal time server, a plausibility gate, rate/range limits, monitoring/detection, procedure>
| Control | Prevent or detect? | D3FEND | 62443 | What it still trusts |
|---|---|---|---|---|

## Single points of failure & residual risk
- **SPOF?** <is there a risk with only one control, or a control the control-logic itself can rewrite —
  the TRITON lesson?>
- **Residual-risk statement:** "Even if <X> is fully owned, <the consequence> stays bounded by ___."

## Does the lab teach AND assess both?
- **Taught?** <where> · **Assessed?** <which graded artifact, or "no" (G6)>

## Gaps (typed)
| # | Gap | Type (G1–G9) | Proposed fix | Priority |
|---|---|---|---|---|
