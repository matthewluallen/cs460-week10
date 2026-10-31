# CS460 Weeks 10–11 — Assessment Rubric

This rubric grades the objectives in `objectives.md`. It encodes the lab's core stance:

> **A clean tool run is not a grade; the reasoning is.**

So each part splits into a small **mechanics** share (did the observable happen — this is
machine-checkable by `assessment/verify-lab.py`) and a larger **reasoning** share (the
student's *finding* about what the evidence means). A green verifier with a weak finding is a
low grade; a red verifier with a sharp, honest finding about *why* still earns most of the
reasoning credit. Mechanics are checked identically on the physical PLC and the twin, so the
target a student used does not change their score.

## Weighting

| Band | Share | Who checks it |
|---|---|---|
| Mechanics — the observable occurred | **40%** | `verify-lab.py <part>` (PASS/FAIL/SKIP) |
| Reasoning — the finding | **60%** | instructor, against the finding standard below |

A `SKIP` (tool or target not available, e.g. Part 7 without the malicious image, or Part 2
without syft/grype) is **not** a penalty: mechanics credit transfers to the reasoning share
for that part, and the student documents what they would run to close it.

## Finding standard (the 60%)

Every part's finding is graded on the same four elements the lab teaches. Each is 0–3:

| Element | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| **Shows** — what the evidence establishes | absent/wrong | restates the command | names the observable | names it with the exact artifact (frame, address, relay, status) |
| **Does not establish** — scope limit | absent | vague | one real limit | the limit that matters here (e.g. "read ≠ trustworthy") |
| **Uncertainty** — what's still open | absent | "none" when there is some | names one | names the one that would change the conclusion |
| **Next observation** — how to reduce it | absent | generic | plausible | a specific, runnable next step |

Reasoning % = (sum / 12) × 60.

## Per-part criteria

| Part | Mechanics (40%) — verifier anchor | Reasoning (60%) — the finding must argue… |
|---|---|---|
| **1 Bus analysis** | `P1`: sample decodes to FC3 / register 25 | that a successful read establishes a value was *reported*, not that it is *trustworthy* or *current* |
| **2 SBOM** | `P2`: SPDX + CycloneDX both generate and differ | one chosen CVE's **reachability** — library-present vs. called-in-code — using the CWE bridge |
| **3 HBOM** | `P3`: six internal I²C devices + 0x77 on the target | each live address reconciled to an HBOM line, and what a *missing* or *extra* address would imply |
| **4 Ladder** | `P4`: edited/example project compiles to IR, pairing holds | the predicted I/O behavior of the change, and the CWE-306 point the rung embodies |
| **5 Wire & run** | `P5`: `di0_start`→relay0&1; e-stop drops motor, latches relay3 | why the output (not the input or the HMI) is ground truth, and what the interlock protects against |
| **6 Unsigned OTA** | `P6`: OTA endpoint accepts an unsigned image | the exact integrity control that is missing (signature verification) and where a signer would sit |
| **7 Supply-chain** | `P7` *(twin)*: armed image changes the relay | why a binary diff **cannot** isolate the payload, and what attestation (reproducible build / SLSA / in-toto) would |
| **8 NTP / Y2038** | `P8`: benign sync clean; armed sync wraps to ~1902 | that unauthenticated time is attacker input, and why the **monotonic** audit clock is the right fix |
| **9 Remediation** | `P9`: on the **secure** build, OTA→401, no wrap, overflow blocked | each attack mapped to the control that now stops it, **plus the residual limits** (e.g. HMAC key is a repo constant) |

## Extensions

| Ext | Mechanics | Reasoning |
|---|---|---|
| **E1 Provenance** | a live `/api/log` line matches its source file:line + CWE | identify the **provenance gap** (output change with no WRITE/FORCE row) as the signature of an unaudited write |
| **E2 Signal Journey** | — | what each of the five lenses can and cannot show about the same byte |
| **E3 Phone FDI** | availability attack (withheld) and integrity attack (spoofed) both observed; plausibility gate built | that the legitimate reading still passes the gate, and the false-data cost/benefit to the attacker |

## Twin ↔ physical transfer (applies to every `[both]` part)

Up to **+1 reasoning point** (within the 3 for *Does not establish* / *Uncertainty*) is earned
when a student who proved an objective on one target states, specifically, **what would have to
be re-checked on the other target and why** — e.g. "the twin's `-icount` virtual clock makes the
Y2038 wrap deterministic; on hardware the RTC drift means I'd re-check the wrap boundary." This
is the CIE habit the two-target environment exists to teach: knowing the scope of your evidence.

## How a grader runs it

```
# Twin (default): boot it, then verify in another shell
./qemu/run.sh                     # or ./qemu/run.sh secure   (for Part 9)
python assessment/verify-lab.py              # all applicable parts
python assessment/verify-lab.py P5           # one part

# Physical StampLC over USB
SERIAL_PORT=/dev/ttyACM0 OTA_HOST=<plc-ip>:80 HMI_HOST=<plc-ip>:8080 \
  python assessment/verify-lab.py
```

The verifier prints `PASS` / `FAIL` / `SKIP` with the evidence line per check; the mechanics
share is that verdict. The reasoning share is the student's written finding, graded above.
