# Control card — CC2 · Read-back integrity gate

- **Risk / chain:** the installed program is not the one the engineer authored (signal-journey link 3;
  Stuxnet T0889). Consequence: a rung energizes relay0 under attacker control.
- **Status:** draft.

## Primary mitigation
- **Control:** the Studio offers **RUN only when the controller's read-back of the installed application
  matches the authored project** — a match is evidence that what runs is what you wrote. · **Design.**
- **Where:** Part 2 (the RUN-enable gate; an unloaded edit greys RUN out — verified in the lab run).
- **D3FEND:** D3-EI (executable integrity), D3-PA (process analysis) · **62443:** FR3 SI, FR2 UC (who may
  download) · **NIST:** SI-7 (software/firmware integrity), CM-3/CM-5 (change control + access) · **CIE:**
  *the program is yours only if the read-back matches.*

## Compensating / defense-in-depth controls
| Control | Prevent or detect? | D3FEND / 62443 | What it still trusts |
|---|---|---|---|
| Signed project / authored-hash check | prevent | D3-EI / FR3 | the signing key |
| Program-change audit (who downloaded, when) | detect | AU-2 / FR6 | the audit path's integrity |
| Dual-approval / mode-key for download | prevent | FR2 UC | the approval process |

## Single points of failure & residual risk
- **SPOF:** the read-back is **the controller reporting on itself** — altered firmware could report
  anything (Parts 6–7). The integrity gate assumes an honest firmware below it.
- **Residual:** "the read-back proves what's installed **as the controller tells it** — not that the
  firmware beneath is trustworthy." That lower layer is CC3.

## Does the lab teach AND assess both?
- **Taught + anchored:** Part 2, `verify-lab.py P2`. **Does-not-establish** is explicitly stated (good).
- **Compensating controls** (signed project, change audit) are **not** present — the gate is a single
  mechanism resting on firmware trust.

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| CC2-a | No signed-project / change-audit compensating control; the gate is single-layer on firmware trust | G4 | name/demonstrate a project-signing or change-audit layer | P2 |
