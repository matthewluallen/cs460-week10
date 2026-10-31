# Assessment

This folder makes the Week-10/11 lab **gradeable from the repo**, on either target — the
physical M5Stack StampLC or its QEMU digital twin — with the same checks.

- **`objectives.md`** — measurable learning objectives (one set per part + the extensions),
  each tagged where it can be demonstrated (`[both]` / `[twin]` / `[offline]`) and mapped to a
  MITRE ATT&CK-for-ICS technique, a CWE, and the cyber-informed-engineering principle it teaches.
- **`rubric.md`** — how each objective is scored: a small **mechanics** share (the observable
  happened — checked by `verify-lab.py`) and a larger **reasoning** share (the student's
  finding, graded against the four-part finding standard).
- **`verify-lab.py`** — the per-part verifier. It attaches to whichever target is running and
  asserts the observable each objective names. The verdict (`PASS`/`FAIL`/`SKIP`) is the
  mechanics share; a `SKIP` (tool or target unavailable) is never a penalty.

## Running the verifier

The checks are identical across targets; only the transport changes.

**Twin (default).** Boot it in one shell, verify in another:
```bash
./qemu/run.sh                       # vulnerable build (Parts 1–8)
python assessment/verify-lab.py     # all applicable parts
python assessment/verify-lab.py P5  # just one part

./qemu/run.sh secure                # for Part 9
python assessment/verify-lab.py P9
```
One self-contained command (boots the twin, verifies, tears it down):
```bash
python assessment/verify-lab.py --boot-twin P1 P3 P5 P6 P8
```

**Physical StampLC** over USB (and Ethernet for the HTTP legs):
```bash
SERIAL_PORT=/dev/ttyACM0 \
OTA_HOST=<plc-ip>:80 HMI_HOST=<plc-ip>:8080 NTP_HOST=<your-host-ip> \
  python assessment/verify-lab.py P3 P5 P6 P8
```
(`pip install pyserial` once for the serial transport.)

## Parts at a glance

| Part | Needs a device? | What it asserts |
|---|---|---|
| P1 Bus analysis | no (offline) | the recorded request decodes to FC03, register 25 |
| P2 SBOM | no (offline) | an SPDX and a CycloneDX SBOM both exist from one tree (SKIP until you run `sbom/run_sbom.sh`) |
| P3 HBOM | yes | `i2cscan` shows the six internal I²C devices + PORT.A 0x77 |
| P4 Ladder | no (offline) | the example projects compile to IR and keep the ST↔ladder pairing (needs `npm i playwright`, else SKIP) |
| P5 Wire & run | yes | `di0_start`→relay0&1; e-stop drops the motor and latches the fault lamp |
| P6 Unsigned OTA | yes | the OTA endpoint is present and advertises an unauthenticated upload |
| P7 Supply-chain | twin, manual | SKIPs (needs the published malicious image; detonation is an interactive step) |
| P8 NTP / Y2038 | yes | benign sync is clean; an armed rogue server wraps the clock to ~1902 |
| P9 Remediation | yes (secure build) | on the secure build the overflow force is blocked and the clock no longer wraps |

## Twin ↔ physical parity

An objective tagged `[both]` returns the **same verdict** on the twin and on hardware, because
the firmware and the console protocol are identical — only `SERIAL_SOCK` vs `SERIAL_PORT` and
the HTTP host differ. When a student proves a `[both]` objective on the twin, the rubric still
asks them to state *what they would re-check on hardware and why*; that transfer argument is a
graded cyber-informed-engineering outcome, not a formality.

CI runs `verify-lab.py` against the twin on every change (the `lab-verify` job), so a
regression in any gradeable check is caught before it reaches a student.
