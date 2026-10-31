# Week 12 — Formally verified updates & the assurance pipeline (Level 3)

Weeks 10–11 taught you to *find* a weakness (Level 1, the vulnerable build) and to *fix* it
(Level 2, the secure build). This week is **Level 3**: you stop asserting that a control works
and **prove** it — with the Z3 SMT solver — and you reason about the **CI/CD pipeline** that has
to carry that proof all the way to a customer without breaking safety, security, or resiliency.

> **New here? Read this first.** The one idea of this week: *a control you cannot prove is a
> control you do not have.* You will run a solver that turns each security control into a
> theorem and either **proves** it (no input can break it) or hands you the **exact input** that
> does. Then you will reason about where proof stops — the parts of a real system you don't own
> — and what **compensating control** goes there instead. The required core is the six steps
> below; **Going deeper** is optional.

**Prerequisites:** Weeks 10–11. You should have already seen each weakness fire on the
vulnerable build and be refused on the secure build (`assessment/verify-lab.py`).

**Setup:** `pip install z3-solver` (once). Everything else runs in your Codespace or local Dev
Container — no device needed; the proofs are about *models* of the firmware controls.

---

## Core path (required)

### 1 — Prove nothing yet: run the solver on the vulnerable build

```bash
python formal-verify/verify_controls.py --profile vuln
```

Every control comes back **VIOLATED**, and Z3 prints the *counterexample* — the concrete input
that breaks it: the Y2038 control fails at `epoch_s=2147483648 (2038-01-19)`, the overflow at
`copy_len=4096`, the Modbus gate at `mode=0`. These are not test cases someone chose; the solver
*searched the whole input space* and returned a witness.

> **Predict first.** Before you run it, for the `modbus_authz` control write down the smallest
> fact that would prove the control is broken. Then compare it to Z3's counterexample.

### 2 — Earn the proof: run it on the secure build

```bash
python formal-verify/verify_controls.py --profile secure
```

Five controls flip to **PROVEN** — Z3 now reports the violation is *unreachable for all inputs*.
But `attestation` stays **VIOLATED**, with a telling counterexample: `hmac_matches=True,
provenance_verified=False`. The secure build's HMAC proves the image wasn't *tampered with in
flight* — it does **not** prove *where it came from*. Integrity is not origin.

### 3 — Level 3: run it on the verified build

```bash
python formal-verify/verify_controls.py --profile verified
# or the whole matrix at once:
python formal-verify/verify_controls.py
```

Now `attestation` is **PROVEN** too: the Level-3 control requires verified build **provenance**
(SLSA / in-toto) in addition to the HMAC, so an image can only deploy if both its integrity and
its origin check out. That extra requirement is the whole difference between Level 2 and Level 3.

### 4 — Read one proof, and find the gap

Open `formal-verify/verify_controls.py` and read **one** control's model — say `force_overflow`.
It encodes the copy as `eff = min(copy_len, 32)` on the secure/verified path and asserts the
violation `eff > 32`. Now open the real code it models, `sbom/firmware/src/plc_core.cpp` (and
`eth_http.cpp`), and line up the model against the implementation.

**This is the cyber-informed-engineering heart of the week:** Z3 proved the *model*. The proof
is only as strong as the model's fidelity to the C. Write down one specific way the model and
the implementation could diverge (a different bound, an untested path, an integer type) — that
divergence is exactly where a bug would hide, and where a compensating control belongs.

### 5 — The assurance pipeline

Read `formal-verify/pipeline-assurance.md`. The formal-verify step you just ran is **stage 2 of
a CI/CD pipeline** (build → **formal verify** → test → sign → attest → gate → deploy → verified
boot), and it runs automatically — see the `formal-verify` job in
`.github/workflows/ci.yml`. For each stage, name its assurance question in CIE terms: does it
protect **safety**, **security**, **resiliency**, or the **customer experience** that rides on
all three? (A green twin test says "these inputs were handled"; a green proof says "no input is
handled wrongly." Say why the pipeline needs both.)

### 6 — Where proof stops: a compensating control

The StampLC lives in a **system of systems** — an upstream sensor, a vendor PLC on the bus, a
cloud historian, an operator workstation — none of which you built or can formally verify. Pick
**one** such boundary from the table in `pipeline-assurance.md`. State: the property you
*cannot* prove, **why** (the boundary you don't own), the **compensating control** you'd place
there, the **MITRE** technique it maps to (D3FEND or ATT&CK-for-ICS mitigation), and the
**residual risk** that remains. A compensating control with no stated residual risk is a claim,
not an engineering decision.

### Finish the core — record your finding

Keep one finding in your notebook. It must contain:

1. **One control, three levels** — for a control you chose, its verdict (PROVEN/VIOLATED) on
   vuln, secure, and verified, and what each transition *buys* (a line-of-code fix, a design
   change, or an attestation requirement).
2. **The model↔implementation gap** — one specific way the Z3 model could diverge from the
   firmware, and how you would close or compensate for it.
3. **One compensating control** — boundary, control, MITRE technique, residual risk.

**That is the required core of Week 12.** The verifier anchors are
`python assessment/verify-lab.py W12` (runs the Level-3 proof) and the `formal-verify` CI job.

---

## Going deeper (optional)

- **Add a control.** Pick a weakness from the Part-9 matrix that isn't modeled yet (e.g. the
  path-traversal log viewer, CWE-22) and add it to `verify_controls.py`: variables, the secure
  rule, the violation. Prove it, and send a counterexample for the vulnerable form.
- **Strengthen a model's fidelity.** Make one model closer to the C — e.g. model the Y2038 store
  as the actual struct field widths, or the overflow as the real `ForceImage` layout and offset
  from `derive-force-offset.py`. Argue what your stronger model now rules out that the simpler
  one didn't.
- **Extend the pipeline.** Wire a second proof target into CI, or add an attestation step that
  verifies a (mock) in-toto statement before the deploy gate, and show the gate fail when the
  provenance is missing.
