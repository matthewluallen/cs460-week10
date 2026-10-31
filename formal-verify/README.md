# formal-verify — proving the security controls with Z3 (Level 3)

The Week 10–11 lab has two build levels: **vulnerable** (teaching) and **secure** (hardened).
This folder adds **Level 3 — verified**: each security control is written as an SMT model and
the **Z3 solver proves it holds for all inputs**, and the proof runs as a CI/CD pipeline gate.

- `verify_controls.py` — the engine. Six controls (OTA signature, Y2038 time, I/O-image
  overflow, Modbus authorization, login lockout, supply-chain attestation), each modeled across
  the three levels. Z3 returns **PROVEN** (the violation is unreachable) or **VIOLATED** with a
  concrete counterexample (the exact date / length / mode that breaks it).
- `pipeline-assurance.md` — the assurance pipeline these proofs live in: build → **formal
  verify** → test → sign → attest → gate → deploy → verified boot, reasoned in cyber-informed-
  engineering terms (safety / security / resiliency / customer experience), mapped to the MITRE
  taxonomy, with compensating-control guidance for the system-of-systems boundaries you can't
  formally verify.

## Run it

```bash
pip install z3-solver                                   # once
python formal-verify/verify_controls.py                 # matrix across vuln / secure / verified
python formal-verify/verify_controls.py --profile verified --assert-proven   # the CI gate
python formal-verify/verify_controls.py --control y2038 --profile vuln        # one cell + counterexample
```

Expected shape: **vuln** → every control VIOLATED (Z3 hands you the exploit input); **secure**
→ the five per-weakness controls PROVEN, `attestation` still VIOLATED (HMAC proves integrity in
flight, not origin); **verified** → all six PROVEN.

## The honest caveat (and the lesson)

Z3 proves properties of a **model** of each control, not of the compiled C directly. The models
in `verify_controls.py` are kept deliberately close to the firmware in `sbom/firmware/src/`
(same 32-byte bound, same REMOTE-mode gate, same 64-vs-32-bit store), but **the proof is only as
strong as the model's fidelity to the implementation.** Closing that model↔implementation gap —
and placing a compensating control wherever you can't — is the subject of the Week 12 lab
([`../week12/`](../week12/)). That gap is not a weakness of the method; recognizing it is the
cyber-informed-engineering point.
