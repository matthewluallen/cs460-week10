# Chain card — Trust chain: memory overflow → force mask (the audit bypass)

> Stage 3, chain #6. Merges into the Signal Journey at **link 4→5** (code → output image). The chain's
> sharpest lesson: an output change with **no audited row**. Parts 6/7 (exploit), 9 (fix).
> Firmware: `plc_core.cpp` (`s_fi` struct: `scratch[32]` then `coil_force_val`@32 / `coil_force_en`@40).

- **Consequence:** RELAY 1 is forced by **memory corruption**, never calling the audited
  `plc_force_coil()` — so the source-attributed log has no entry (the provenance gap).
- **Status:** draft.

## Links
| # | From → To | Lens | Establishes | Does NOT establish | Technique | CWE | Obs? |
|---|---|---|---|---|---|---|---|
| 1 | overlong `/api/tag?data=…` (48 bytes) → unbounded `memcpy` | Wire→Code | a too-long write is accepted into a fixed buffer | — | T0866 Exploitation (**reach only**) | **CWE-787** Out-of-bounds Write / CWE-788 | yes |
| 2 | overflow past `scratch[32]` reaches `coil_force_val`/`_en` | Code · `plc_core.cpp:42-55` | the force mask is corrupted by the overflow | — | — | **CWE-787/788** | inferred (the struct layout) |
| 3 | relay0 forced ON **without** `plc_force_coil()` | Code→Bus | the output changes | that any audited path ran — it did **not** | **T0835** Manipulate I/O Image | — | yes (`io` → relay0=1) |
| 4 | `plc> log` shows **no WRITE/FORCE row** for the change | Code | the audit trail **missed** it → detection gap | that nothing happened (it did) | T0872 Indicator Removal (analog) | CWE-778 Insufficient Logging | yes (log vs io) |

> Requires the controller in **RUN with the motor program** (outputs active) — see backlog B06; otherwise
> the demo is silent.

## Bowtie — barriers
- **Bounds-checked copy** (the secure fix) — D3FEND: this is **memory bounds/input validation**, *not*
  D3-SFCV stack-canary (the buffer is a file-scope `static` in .bss, not on the stack — see gap O1).
- **The audited force path as the only write path** — the design control: outputs change only through a
  logged, source-attributed call (CIE: every actuation has provenance).
- **Memory-safe language / exploit mitigations** (ASLR/canaries) — context; limited on ESP32.

## Residual risk
"Even if someone reaches the tag endpoint, RELAY 1 / the evidence stays bounded by ___" — on the secure
build, by the bounds check + the audited-only write path; the detection gap is the teaching climax.

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| B14 | Threat-Map maps this to **T0866**; the *effect* is I/O-image manipulation → **T0835** (keep T0866 for the reach only). | G2 | Stage 5 | P1 |
| O1 | Threat-Map D3FEND = **D3-SFCV (stack canary)**, but the target is a **static/.bss** buffer — the correct countermeasure is bounds-checking / input validation (and the CWE is 787/788, which the code says but the map's defense doesn't match). | G2 | Stage 5: fix the D3FEND cell. | P1 |
| O2 | Add **CWE-778 Insufficient Logging** for link 4 — the audit *gap* is itself a named weakness, currently unmapped. | G8 | Stage 5 | P2 |
| B06 | Demo needs RUN + motor program (else silent). | G7 | lab text (in the B03–B12 batch). | P1 |
