# Week 12 — Assessment Rubric

Same stance as the rest of the lab: **a clean solver run is not a grade; the reasoning is.** The
proofs are mechanical (Z3 returns PROVEN or a counterexample — `assessment/verify-lab.py W12`
checks it); the grade is in the attack narratives, the fidelity analysis, and the
compensating-control decisions.

## Weighting

| Band | Share | Who checks it |
|---|---|---|
| Mechanics — the proofs ran and the code extension works | **35%** | `verify_controls.py` verdicts · `verify-lab.py W12` · the submitted diff |
| Reasoning — the finding | **65%** | instructor, against the finding standard in `../assessment/rubric.md` |

## Per-part criteria (assignment Parts A–D)

| Part | Mechanics (35%) | Reasoning (65%) — the finding must argue… |
|---|---|---|
| **A — Counterexample is the attack** | both controls run across all three levels; verdicts quoted | each vulnerable counterexample as a concrete exploit (CWE + ATT&CK-for-ICS technique), and precisely what UNSAT guarantees and does not |
| **B — Model vs. implementation** | the control's model and its `sbom/firmware/src/` source both cited | one specific fidelity gap, whether it could hide a real bug, and how to close or compensate for it |
| **C — Extend the proof** | new/strengthened model PROVEN on secure/verified and VIOLATED (counterexample) on vuln; diff + before/after output submitted | what the new or stronger model now rules out that the old one didn't |
| **D — Pipeline & boundaries** | — | each stage mapped to a CIE question + MITRE mitigation (and why verify ≠ test); two compensating controls with boundary, control, MITRE technique, and **residual risk** |

## Level-distinction credit

Up to **+1 reasoning point** for a finding that states cleanly what each *level transition*
buys: Level 1→2 is a line-of-code fix or a design change; Level 2→3 is an **attestation
requirement** (integrity → origin). A student who treats "secure" and "verified" as the same
thing has missed the week.

## The residual-risk rule

A compensating control submitted **without a stated residual risk** earns no reasoning credit
for Part D. The point of a compensating control is that it reduces reachability or consequence
while the underlying property stays unproven — naming what remains is the engineering.

## How a grader runs it

```bash
pip install z3-solver
python formal-verify/verify_controls.py                       # the three-level matrix
python assessment/verify-lab.py W12                           # the Level-3 mechanics check (exit 0)
# Part C: apply the student's diff, then re-run their control:
python formal-verify/verify_controls.py --control <name>
```
