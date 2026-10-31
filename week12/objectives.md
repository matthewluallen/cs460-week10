# Week 12 — Learning Objectives (measurable)

Level 3 of the security arc: from *fixing* a control to **proving** it, and reasoning about the
CI/CD pipeline that carries the proof to a customer. All objectives are demonstrable with the
Z3 engine in `formal-verify/` — no device needed (`[offline]`), because the proofs are about
models of the firmware controls. Mapping key as in `../assessment/objectives.md`: MITRE
ATT&CK-for-ICS / CWE / CIE principle. Verdicts use `assessment/verify-lab.py W12` and the
`formal-verify` CI job as anchors.

## Course-level outcomes

By the end of Week 12 a student will be able to:

1. **Prove a security control** by modeling it as an SMT problem and showing (with Z3) that its
   violation is unreachable for all inputs — and read the **counterexample** when it is not.
2. **Distinguish levels of assurance** — a tested example vs. a formal proof, and integrity
   (HMAC) vs. origin (attestation) — and say what each does and does not guarantee.
3. **Reason about a CI/CD assurance pipeline** in cyber-informed-engineering terms (safety,
   security, resiliency, customer experience) and map its stages to the MITRE taxonomy.
4. **Design a compensating control** for a system-of-systems boundary that cannot be formally
   verified, naming its MITRE technique and its residual risk.

## Per-step objectives

| # | SWBAT (measurable) | Observable / anchor | Mapping |
|---|---|---|---|
| 1 | **Run** the verifier on the vulnerable build and **interpret** each counterexample as the exploit input it is | every control VIOLATED; student explains one counterexample (date/length/mode) | CWE per control · ATT&CK T0835/T1692.001 (formerly T0855)/T0830 · CIE: an unproven control is no control |
| 2 | **Explain** why the secure build proves five controls but leaves `attestation` violated | `secure` matrix; student states integrity ≠ origin | CWE-494 · ATT&CK T0862 · CIE: trust needs provenance |
| 3 | **Show** that the verified (Level 3) build proves all six, and **state** what the provenance requirement adds | `verified` all PROVEN | SLSA/in-toto · ATT&CK M0947 · CIE |
| 4 | **Analyze** one model against its firmware source and **identify** a model↔implementation fidelity gap | named divergence between `verify_controls.py` and `sbom/firmware/src/` | CIE: a proof is only as strong as its model |
| 5 | **Map** each CI/CD pipeline stage to a CIE assurance question and a MITRE mitigation | stage→CIE→MITRE table from `pipeline-assurance.md`; why verify ≠ test | ATT&CK-ICS M0945/M0946/M0947 · D3FEND |
| 6 | **Design** a compensating control for a boundary you don't own, with its MITRE technique and residual risk | boundary · control · MITRE · residual risk | ATT&CK-ICS M0930/M0801 · D3FEND · CIE |

## Anchor

`python assessment/verify-lab.py W12` reruns the Level-3 proof (`verify_controls.py --profile
verified --assert-proven`) as the mechanics check; the `formal-verify` CI job runs it on every
change so a regression that re-opens a control fails the build.
