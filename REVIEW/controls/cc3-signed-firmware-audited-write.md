# Control card — CC3 · Signed firmware + audited write path

- **Risk / chain:** untrusted code runs the scan (trust-firmware: unsigned OTA / supply-chain), or an
  output is changed outside the audited path (trust-memory-overflow). Consequence: relay0 moves with no
  authentic cause and, for the overflow, **no audited row**.
- **Status:** draft.

## Primary mitigation
- **Control (code provenance):** **signed firmware** — HMAC on the secure build (Part 9), **public-key
  signatures + a build record** in Week 12. · **Control (output provenance):** outputs change **only**
  through the audited `plc_force_coil()` path, and the overflow is closed by a **bounds-checked copy**
  (CWE-787 fix). · **Code + design.**
- **Where:** Parts 6/7 (the attacks), Part 9 (`verify-secure`: OTA→401), Week 12 (Z3-proved signatures).
- **D3FEND:** D3-EI / D3-DENCR (flash encryption) / bounds-checking (not D3-SFCV — see gap CO1) / D3-OTF
  (outbound filtering) · **62443:** FR3 SI (integrity), FR1 IAC (update auth), FR5 RDF (egress) · **NIST:**
  SI-7, SA-10/11 (secure dev), **SR-4/SR-11 (supply-chain provenance)**, SC-39 (process isolation), AU-2
  (audit the force path) · **CIE:** *every actuation has provenance; trust needs a build record, not a
  byte-diff.*

## Compensating / defense-in-depth controls
| Control | Prevent or detect? | D3FEND / 62443 | What it still trusts |
|---|---|---|---|
| Secure boot + flash encryption | prevent | D3-DENCR / FR3 | **absent on this board (residual)** |
| Reproducible build + SLSA / in-toto attestation | detect provenance | SR-4 / FR3 | the attestation infra |
| Egress filtering / no outbound C2 | prevent + detect | D3-OTF / FR5 | the network boundary |
| The audited write path as the *only* path to an output | detect (the gap is the signal) | AU-2 / FR6 | memory safety below it |

## Single points of failure & residual risk
- **SPOF:** the secure build's integrity rests on an **HMAC key compiled into every image** — anyone who
  recovers it can sign an image. No secure boot / flash encryption.
- **Residual:** "Even on the secure build, updates are trustworthy only until the shared HMAC key leaks;
  Week 12 replaces it with public-key signatures + a build record." The overflow's teaching climax is the
  **detection gap** (output changed, no audited row).

## Does the lab teach AND assess both?
- **Taught + anchored:** Part 9 `verify-secure` (OTA refusal, overflow-bounded) + Week 12 Z3 proofs.
- **Prose-only (not demonstrated):** SLSA/in-toto provenance, secure boot, egress filtering.

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| CO1 | overflow D3FEND mapped D3-SFCV (stack canary) but the buffer is .bss static → bounds-check/input-validation | G2 | Stage 5 | P1 |
| CO2 | the audit gap itself is unmapped — add CWE-778 (insufficient logging) + AU-2 | G8 | Stage 5 | P2 |
| CF2/CF3 | SLSA/in-toto + secure-boot + egress filtering are prose-only | G9/G4 | mark as conceptual, or add a signed-build/egress demo | P2 |
| B07 | the secure build's positive control can't run (project not active on boot) — can't currently prove the fix end-to-end | G5 | fix secure boot-load (see backlog B07) | P1 |
