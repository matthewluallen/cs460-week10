# Week 12 assignment — Prove it, then compensate for what you can't

**Goal.** Move from *fixing* a control to *proving* it, and then reason about the CI/CD pipeline
and the system-of-systems boundaries where proof stops. Everything builds on
`formal-verify/` and `pipeline-assurance.md`.

Work in your own repo copy. Keep the `verify_controls.py` output for anything you claim — a
claim without the solver's verdict is not evidence.

## Part A — The counterexample is the attack (prove × 2)

Choose **two** controls. For each:

1. Run all three levels (`--control <name> --profile vuln|secure|verified`).
2. For the **vulnerable** form, turn Z3's counterexample into an attack narrative: what the
   input is, what the device does, the **CWE** it exploits, and the **MITRE ATT&CK for ICS**
   technique it realizes. (The counterexample *is* the exploit input — explain why.)
3. For the **secure/verified** form, state in one sentence what UNSAT actually guarantees — and,
   just as important, what it does **not** (it is a statement about the model, over the modeled
   inputs).

## Part B — Model vs. implementation (the fidelity gap)

Pick **one** of your two controls. Open its model in `verify_controls.py` and the firmware it
abstracts in `sbom/firmware/src/`. Write a short fidelity analysis:

- One concrete way the model could be *more faithful* to the C (a type width, a bound, a branch
  or an entry point the model doesn't capture).
- Whether that gap could hide a real bug, and how you would close it — a stronger model, a twin
  test that exercises the path, or a compensating control if neither is possible.

This is the cyber-informed-engineering core of the assignment: a proof you cannot connect to the
running system is reassurance, not assurance.

## Part C — Extend the proof (code)

Do **one** of:

- **Add a control.** Model a Part-9 weakness that isn't covered yet (e.g. CWE-22 path traversal
  in the log viewer, or CWE-319 cleartext transport). Add variables, the secure rule, and the
  violation; show it **PROVEN** on secure/verified and a **counterexample** on vuln.
- **Strengthen a model.** Make an existing model measurably closer to the implementation (e.g.
  the real `ForceImage` offset from `sbom/firmware/tools/derive-force-offset.py`, or the actual
  struct field widths for the Y2038 store). State what your stronger model now rules out.

Submit the diff and the `verify_controls.py` output before and after.

## Part D — The assurance pipeline and its boundaries

1. **Map the pipeline.** For each stage in `pipeline-assurance.md`, name its assurance question
   in CIE terms — **safety**, **security**, **resiliency**, or the **customer experience** that
   rides on them — and the MITRE mitigation/technique it embodies. Say explicitly why the
   **formal-verify** stage is not redundant with the **test** stage.
2. **Compensate where you can't prove.** The TBD controller is one node in a system of systems. Choose
   **two** boundaries you do not own (an upstream sensor, a vendor PLC, a cloud historian, the
   operator workstation, an unauthenticated time source). For each, give: the property you
   cannot prove, **why** (the boundary), the **compensating control**, its **MITRE** technique
   (D3FEND or ATT&CK-for-ICS mitigation), and the **residual risk** that remains after it.

## Deliverable

A short report (or a `finding.md` in your repo) with Parts A–D, plus:

- the exact `verify_controls.py` commands and their verdicts for every claim;
- for Part C, the diff and before/after proof output;
- your individual contribution and any assistance.

Run `python assessment/verify-lab.py W12` before you submit — it reruns the Level-3 proof as the
mechanics check. Graded on `week12/rubric.md`: the **mechanics** are the solver verdicts; the
larger **reasoning** share is your attack narratives, the fidelity gap, and the
compensating-control decisions with their residual risk.
