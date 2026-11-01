# CS460 Weeks 10–11 — Learning Objectives (measurable)

This lab runs on two interchangeable targets: a **physical M5Stack StampLC** and its
**QEMU digital twin** (which boots the *real, unmodified* firmware). Every objective below
is written so it can be demonstrated and **assessed identically on either target** — the
console commands, the protocol on the wire, and the observable effect are the same; only the
transport differs (a USB serial port for the physical PLC, a Unix socket for the twin). The
per-part verifier (`assessment/verify-lab.py`) attaches to whichever target is running and
checks the same observable, so a student can prove an objective on the twin today and on
hardware at the bench tomorrow and get the same PASS.

**Parity legend** — where an objective can be demonstrated:
`[both]` physical PLC and twin · `[twin]` twin only · `[offline]` no device (repo tools only).

**Mapping key** — each objective ties to the course's throughline across the taxonomy spine:
MITRE **ATT&CK for ICS** technique (T0xxx/T16xx) · **CWE** weakness class · **CAPEC** attack pattern ·
MITRE **EMB3D** device threat · **D3FEND** countermeasure · **control standard** (**IEC 62443** FR/SL ·
**NIST SP 800-53/800-82**) · **CIE** = cyber-informed-engineering principle.

> **Authoritative cross-walk:** the full per-attack mapping across all of these lives in
> `web/public/threat-map.html` (and is reviewed in `REVIEW/`). This file, the Threat Map, and the lab
> "Sources:" lines are the three sources of truth and must agree — if they drift, the Threat Map +
> `REVIEW/02-coverage-matrix.md` win. (Example reconciled 2026-11: the overflow force is **T0835**
> Manipulate I/O Image for the effect, **T0866** only for the remote reach.)

Objectives use measurable verbs (decode, generate, reconcile, force, demonstrate, trace,
prove, refute, remediate, verify). "Reasoning" criteria are assessed against the four-part
finding standard the lab uses throughout: *what the evidence shows / what it does not
establish / the remaining uncertainty / the next observation that would reduce it.*

---

## Course-level outcomes

By the end of Weeks 10–11, a student will be able to:

1. **Trace one control action across every layer** — wire bytes → protocol frame → firmware
   source line → ladder rung → physical relay — and state at each layer what the evidence
   does and does not establish. *(CIE: evidence has a layer and a scope.)*
2. **Produce and reconcile** a software and a hardware bill of materials, and judge a
   vulnerability by **reachability**, not mere presence. *(CIE: an SBOM hit is a question, not a verdict.)*
3. **Demonstrate and then remediate** representative ICS weaknesses — unsigned OTA, a
   supply-chain payload, unauthenticated network time / Y2038, and a memory-corruption I/O
   force — mapping each to an ATT&CK-for-ICS technique and a CWE.
4. **Apply the CIE stance** that *every value entering control logic is a claim, not a fact*,
   and **verify a control's behavior on both a physical PLC and its digital twin**, arguing
   why twin evidence does or does not transfer to hardware.

---

## Per-part objectives

**Section A · Build** — Parts 1–2

### Part 1 — Wire & run (input → logic → output) `[both]`
**SWBAT** drive one input through the ladder to one output and **observe** the physical relay
change, then **demonstrate** the fail-safe interlock of the factory motor program
(`motor-start-stop-estop`: Start `start_pb` = IN1/di0, Stop `stop_pb` = IN2/di1, E-stop
`estop_ok` = IN3/di2 wired **N.C.** so 1 = healthy and 0 = tripped or wire cut, Reset
`reset_pb` = IN4/di3; motor = RELAY 1/relay0, run light = RELAY 2/relay1, fault lamp =
RELAY 4/relay3).
- **Observable:** in RUN with `estop_ok` = 1, pressing `start_pb` energizes relay0 **and**
  relay1 and they **stay on** after the button is released (seal-in); `stop_pb` drops them;
  `estop_ok` = 0 drops the motor and latches the fault lamp (relay3 → 1); restoring
  `estop_ok` = 1 does **not** restart the motor and does **not** clear the fault; `reset_pb`
  clears the fault but the motor stays off; only a new `start_pb` press runs it again.
- **Twin note:** the twin's input byte boots as 0, so IN3 (the N.C. E-stop) reads **tripped**
  until something drives it to 1 — exactly what a real N.C. loop does with a cut wire. The
  check therefore holds `estop_ok` at 1 first (`force estop_ok 1`) and pulses `reset_pb`; a
  student's predesigned circuit must do the same (E-stop slide ON at start-up).
- **Anchor:** `verify-lab.py P1` (forces the inputs by tag and polls `io` for the relay state
  on the running target — identical on hardware and twin — and releases every force at the end).
- **Mapping:** ATT&CK **T0831** (Manipulation of Control), **T1692.001** (Unauthorized Message: Command Message; formerly T0855) · CIE: the output is the ground truth.

### Part 2 — Ladder logic `[offline]`
**SWBAT** modify a prebuilt ladder program by enabling one rung (or adding one function) and
**compile** it to device IR without error, **predicting** the resulting I/O behavior.
- **Observable:** the edited project loads clean and compiles to IR; the ST↔ladder pairing holds.
- **Anchor:** `verify-lab.py P2` (compiles the example projects via the Studio compiler and
  checks IR + pairing; this is the `studio-regression` check) · prediction graded by rubric.
- **Mapping:** ATT&CK **T0889** (Modify Program) + **T0843** (Program Download) context (Modify Control Logic, formerly T0833, is deprecated in ATT&CK for ICS v19) · CWE-306 (missing auth on a critical function, taught in the rung) · CIE: logic is code and gets reviewed.

**Section B · Analyze** — Parts 3–5

### Part 3 — Bus analysis `[offline]`
**SWBAT** decode a recorded Modbus RTU exchange to its function code, address, and payload,
and **state** what the exchange does and does not establish about the device's state.
- **Observable:** the student's decode of the sample frame names FC3, register 25, and the
  one-register read; the finding separates "a value was read" from "the value is trustworthy."
- **Anchor:** `verify-lab.py P3` (runs `bus-analysis/decode.sh` on the committed sample and
  confirms the FC/register decode) · student finding graded by rubric.
- **Mapping:** ATT&CK **T0861** (Point & Tag Identification), **T0801** (Monitor Process State) · CIE: a read is not a write.

### Part 4 — HBOM `[both]`
**SWBAT** enumerate the StampLC's I²C devices and **reconcile** the live bus scan against the
hardware bill of materials.
- **Observable:** `i2cscan` reports the six internal devices (0x20, 0x32, 0x40, 0x43, 0x48,
  0x59) plus the PORT.A device at 0x77, and each is matched to an HBOM line.
- **Anchor:** `verify-lab.py P4` (asserts the six internal addresses + 0x77 on the running target).
- **Mapping:** ATT&CK **T0888** (Remote System Information Discovery) · CIE: the parts list must match the bus.

### Part 5 — SBOM `[offline]`
**SWBAT** generate an SPDX and a CycloneDX SBOM from the same source tree, **explain** why
their component counts differ, and **classify** a scanner hit as library-present vs.
reachable-in-code.
- **Observable:** two SBOMs exist from one tree; the student states the count divergence and
  picks one CVE to argue reachability for/against using the CWE bridge.
- **Anchor:** `verify-lab.py P5` (confirms both formats generate and differ; SKIPs if
  syft/grype absent) · reachability argument graded by rubric.
- **Mapping:** ATT&CK **T0862** (Supply Chain Compromise, recon side) · CWE: library-vs-reachable · CIE: presence ≠ exploitability.

**Section C · Break** — Parts 6–8

### Part 6 — Firmware & the unsigned update `[both]`
**SWBAT** **prove** that the OTA endpoint accepts an unsigned/unauthenticated image and
**state** the integrity property that is missing.
- **Observable:** the W5500 HTTP OTA endpoint on :80 advertises `/api/upload` and accepts an
  unsigned image; the student names signature verification as the absent control.
- **Anchor:** `verify-lab.py P6` (confirms the OTA endpoint and the unsigned-accept path on
  the vulnerable build).
- **Mapping:** ATT&CK **T1693.001** (Modify Firmware: System Firmware; formerly T0857), **T0845** (Program Upload) · CWE-347 (Improper Verification of Cryptographic Signature) · CIE: an update path is a trust path.

### Part 7 — Supply-chain kill switch `[twin]`
**SWBAT** **detonate** a dormant OTA payload through a compromised update source, **trace**
its effect to a specific relay, and **explain** why a binary diff cannot isolate it.
- **Observable:** the armed image changes relay behavior on cue; the student argues that
  because the payload relays out the whole app partition, diffing shows millions of changed
  bytes and cannot localize it — a reproducible reference build or SLSA/in-toto attestation can.
- **Anchor:** `verify-lab.py P7` (twin only — requires the malicious image; SKIPs if absent) ·
  provenance argument graded by rubric.
- **Mapping:** ATT&CK **T0862** (Supply Chain Compromise) · CWE-506 (Embedded Malicious Code) · CIE: provenance needs attestation, not inspection.

### Part 8 — NTP adversary-in-the-middle & Y2038 `[both]`
**SWBAT** **spoof** the unauthenticated network time the controller trusts, **demonstrate**
the 32-bit clock wrapping to 1902, and **prove** the audit log stayed on a monotonic clock.
- **Observable:** a rogue SNTP server steps the clock past 2038; `clk` shows WRAPPED (~1902);
  the audit log timestamps remain monotonic (`millis()`), unaffected by the wall-clock wrap.
- **Anchor:** `verify-lab.py P8` (benign sync shows no truncation, armed sync shows the wrap).
- **Mapping:** ATT&CK **T0830** (Adversary-in-the-Middle), **T0837** (Loss of Protection) · CWE-190/191 + CWE-197 (signed-32-bit overflow / truncation) · CIE: unauthenticated time is attacker-controlled input.

**Section D · Secure** — Part 9

### Part 9 — Remediation & compensating controls `[both]`
**SWBAT** build the hardened (secure) profile and **verify** behaviorally that Parts 6–8 and
the overflow force now **fail**, and **record** the residual limits.
- **Observable:** on the secure build, OTA returns 401, the clock no longer wraps, and the
  overlong `/api/tag` write leaves relay0 off; the student notes residual limits (e.g., the
  HMAC key is a repo constant).
- **Anchor:** `verify-lab.py P9` / `remediation/verify-secure.py` (drives the running **secure**
  target and asserts each attack is refused).
- **Mapping:** ATT&CK **T0858** mitigation framing · CWE-347/306/190 closed · CIE: a fix is proven by effect, not by a banner.

---

## Extensions (Weeks 10–11)

### E1 — Static ↔ dynamic provenance `[both]`
**SWBAT** match a live runtime event (`/api/log`) to the firmware **source line + CWE** that
emitted it, and **identify the provenance gap** — an output change with *no* matching
WRITE/FORCE log row — as the signature of an unaudited (memory-corruption) write.
- **Mapping:** CWE-778 (Insufficient Logging) · CIE: every control write should be attributable.

### E2 — Signal Journey `[both]`
**SWBAT** follow one byte across five lenses (Wireshark → sigrok/PulseView → firmware source
→ ladder → physical relay) and **explain** what each lens can and cannot show.
- **Mapping:** ties Parts 1/2/3/7 together · CIE: one fact, many vantage points.

### E3 — Phone-as-sensor false-data injection `[both]`
**SWBAT** mount an **availability** attack (withhold a sensor the PLC trusts) and an
**integrity** attack (spoof its value), then **build a plausibility gate** and **prove** the
legitimate reading still passes.
- **Mapping:** ATT&CK **T1692.002** (Unauthorized Message: Reporting Message; formerly T0856 Spoof Reporting Message), **T0814** (Denial of Service),
  **T0815** (Denial of View); ATLAS **AML.T0043** · CIE: every sensor value is a claim, not a fact.

---

## Twin ↔ physical parity contract

An objective tagged `[both]` must produce the **same verifier verdict** on the twin and on a
physical StampLC. This holds because:

- The firmware is identical — the twin boots the same image that flashes to the board.
- The console protocol is identical — `mode`, `force`, `io`, `clk`, `log`, `i2cscan`,
  `ntpsync` behave the same over a USB serial port or the twin's Unix socket.
- The verifier selects the transport at run time (`SERIAL_PORT=/dev/tty…` for hardware,
  `SERIAL_SOCK=…` for the twin) and the HTTP endpoints by host (`OTA_HOST`, `HMI_HOST`), so
  the **assertions do not change** between targets.

Where parity does **not** hold, it is stated: Part 7 is `[twin]` (needs the published
malicious image), and anything requiring a logic analyzer on the physical bus is bench-only
(the twin provides the virtual tap). When a student proves a `[both]` objective on the twin,
the finding should still note *what would have to be re-checked on hardware and why* — that
argument is itself a graded CIE outcome.
