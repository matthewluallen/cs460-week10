# Level 3 — the assurance pipeline: formally verified updates, end to end

Level 1 (vulnerable) and Level 2 (secure) are about the *device*. **Level 3 is about the
pipeline that produces and ships the device's software**, and about proving — not asserting —
that each security control holds. It is cyber-informed engineering applied to CI/CD: the build
system is itself a safety system, and its output reaches a customer.

The through-idea: **a control you cannot prove is a control you do not have.** Z3 turns each
control into a theorem (`formal-verify/verify_controls.py`); the pipeline runs those theorems
as a gate, so a regression that re-opens a weakness fails the build instead of shipping.

## The pipeline, stage by stage

```
 source ─▶ build(profile) ─▶ FORMAL VERIFY (Z3) ─▶ twin/unit test ─▶ sign ─▶ attest (SLSA/in-toto)
        ─▶ release gate ─▶ deploy ─▶ verified boot ─▶ runtime monitor ─▶ (feedback to source)
```

Each stage answers one assurance question, in CIE terms — **safety** (does it keep the plant
and people safe?), **security** (can an adversary subvert it?), **resiliency** (does it fail
safe and recover?), and the **customer experience** that sits on top of all three (the operator
who must trust what the glass says, and never receive a bricked or hostile update).

| Stage | Assurance question | Control / evidence | CIE lens | MITRE |
|---|---|---|---|---|
| **build(profile)** | Which branches compiled? | `build_profile_check.py` enforces exactly one of the demo/secure flags | security | D3FEND: Build-process integrity |
| **formal verify (Z3)** | Does each control hold for *all* inputs? | `verify_controls.py --assert-proven` — UNSAT of every violation | safety + security | ATT&CK-ICS M0947 (Audit), M0801 (Access Mgmt) |
| **twin/unit test** | Does the *implementation* behave as the model claims? | `qemu/smoke-test.py`, `assessment/verify-lab.py`, bench pytest | resiliency | D3FEND: Dynamic analysis |
| **sign** | Is the artifact tamper-evident in flight? | HMAC/signature over the image (the Level-2 control, now proven) | security | ATT&CK-ICS M0945 (Code signing) |
| **attest (SLSA/in-toto)** | Can we prove *where it came from*? | signed build provenance; `attestation` control proven only at Level 3 | security + CX | ATT&CK-ICS M0947; D3FEND: Software provenance |
| **release gate** | Did every prior stage pass? | the gate is the conjunction of the stages above | safety | D3FEND: Release authorization |
| **deploy / verified boot** | Will the device run only attested code? | signature + rollback-version check at boot | safety + resiliency | ATT&CK-ICS M0946 (Boot integrity) |
| **runtime monitor** | If a control's *assumption* breaks in the field, do we see it? | the provenance feed + the audit log (`/api/log`) | resiliency + CX | ATT&CK-ICS M0931 (Network monitoring) |

The formal-verify stage is the one most teams skip and the one that makes this Level 3: the
other stages *test examples*; Z3 *proves the absence of a counterexample*. A green twin smoke
test says "these inputs were handled"; a green Z3 proof says "no input is handled wrongly," for
the modeled property.

## Where formal proof stops: system-of-systems integration

A proof is only as wide as the boundary you own. The StampLC does not run alone — it sits in a
system of systems: an upstream sensor it did not manufacture, a vendor PLC on the same bus, a
historian or cloud service, an operator's workstation. **You cannot formally verify code and
interfaces you neither build nor control.** At each such boundary the honest move is a
*compensating control*: it does not prove the property, it *reduces the reachability or the
consequence* of the property failing, and you record what residual risk remains.

| Property you cannot prove end-to-end | Why (the boundary) | Compensating control | MITRE (D3FEND / ATT&CK-ICS) | Residual risk |
|---|---|---|---|---|
| The upstream sensor reports true physical state | 3rd-party sensor firmware, not yours | Plausibility gate on the value + cross-check a second source (the Week-11 phone-FDI defense) | D3FEND: Input validation; ATT&CK-ICS M0947 | A physically consistent but false reading still passes |
| A vendor PLC on the shared bus behaves | Closed vendor firmware | Network segmentation + an allowlisting gateway in front of it | ATT&CK-ICS M0930 (Network segmentation), M0807 | A compromised vendor device inside the segment still reaches peers |
| The cloud historian stores data faithfully | Provider-operated service | Signed, append-only local audit as the source of truth; reconcile on read | D3FEND: Log analysis | A divergence is detected after the fact, not prevented |
| The operator workstation is uncompromised | IT-managed endpoint, not OT | REMOTE-mode gate on writes (proven) + out-of-band confirmation for mode changes | ATT&CK-ICS M0801, M0937 | A valid operator coerced or spoofed in REMOTE still commands the plant |
| Time from the network is true | Unauthenticated time source | Authenticated time where available; monotonic audit clock (proven) + reject implausible jumps | ATT&CK-ICS M0916; D3FEND: Clock integrity | The displayed wall time can still be wrong; only the audit order is guaranteed |

The rule for the write-up: **name the boundary, state the control, cite the MITRE technique,
and record the residual risk.** A compensating control with no stated residual risk is a claim,
not an engineering decision.

## Customer experience is the top of the stack

Safety, security, and resiliency are not separate from the customer experience — they *are* it,
expressed as what the operator can trust:

- **Safety as CX:** the operator must never be handed a control that lies. Verified boot + the
  proven I/O-image bound mean the relay state on the glass is the relay state in the field.
- **Security as CX:** a signed, attested update means the image the customer receives is the
  one the vendor built — not a supply-chain substitute. The `attestation` proof is what lets
  you say that with a straight face.
- **Resiliency as CX:** rollback protection + fail-safe interlocks mean a bad update degrades
  to a safe, recoverable state instead of a brick or a runaway. The pipeline's job is that no
  update reaches the customer without this guarantee.

A pipeline that ships only PROVEN controls, attests their provenance, and documents the
compensating controls at every boundary it does not own is what "formally verified updates"
means here — and it is the subject of the Week 12 lab.
