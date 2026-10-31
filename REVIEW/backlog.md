# Backlog — running gap register

Single prioritized list; grows each slice. **Pri** is provisional (Stage-0 model = exploitability ×
device-exposure × consequence, SSVC-style); we re-rank in Stage 7. **Src:** LR = lab run-through
(2026-10-31), TM = Threat-Map/coverage (Stage 1), RB = 2026-10-30 Review Board.

| ID | Gap | Type | Pri | Src | Status |
|---|---|---|---|---|---|
| B01 | verify-lab P1 spurious FAIL when run after the Part 1 sequence | G5 | P1 | LR | **fixed in PR #12** |
| B02 | Circuit phone card reads wrong Modbus regs (50/51/52 vs 140-165) | G2 | P1 | LR | **fixed in PR #12** |
| B03 | Part 2 text: "delete seal-in branch = jog" is wrong (MX1 new-press contact also drops it) | G7 | P1 | LR | open |
| B04 | Part 3 setup collides on :8088 (student's own twin still running) — needs "stop first" step | G1 | P1 | LR | open |
| B05 | Part 3 tap in the plant has no sigrok decode (sigrok-cli not in stations/plc image) | G1 | P2 | LR | open |
| B06 | Part 9 overlong-write demo needs RUN + motor program; silent in PROGRAM mode | G7 | P2 | LR | open |
| B07 | Secure build ships without the factory motor program → 2 verify-secure positive-control FAILs | G5 | P1 | LR | open |
| B08 | console.sh / verify-lab misleading when the plant is up (twin is in the container) | G7 | P3 | LR | open |
| B09 | share.sh can't make :8090 public from the Codespace terminal; lab should say so | G7 | P3 | LR | open |
| B10 | Studio toolbar says "USB" when connected to the twin | G7 | P3 | LR | open |
| B11 | Part 7 has no automated proof of the no-input relay move (verify-lab P7 = manual SKIP) | G5 | P2 | LR | open |
| B12 | stations plc entrypoint probes during the firmware's ENTER window (opens Wi-Fi menu) | G1 | P3 | LR | open |
| B13 | Threat-Map: 3 headline attacks uncovered (unsigned OTA T1693.001, supply-chain T0862, NTP/Y2038 T0830) | G8 | P1 | TM | open |
| B14 | Overflow-force mapped T0866; effect is I/O-image manipulation → T0835 | G2 | P2 | TM/RB | open |
| B15 | Three sources of truth diverge (lab Sources vs Threat-Map vs objectives); no sync check | G7 | P1 | TM | open |
| B16 | No control-standard lens (IEC 62443 FR/SL, NIST 800-82/800-53) anywhere | G8 | P1 | TM | open |
| B17 | No ATT&CK **Enterprise** mapping for the IT side (HMI container, engineering station) | G8 | P2 | TM | open |
| B18 | CWE-78 command injection is pattern-only (no shell) — keep out of "observable" columns | G9 | P2 | TM/RB | open |
| B19 | `panel.html` unusable non-visually (canvas-only, A/S/D keys, no text mirror) | — (a11y) | P2 | RB | open |
| B20 | Web tools (Threat Map, Provenance, Signal Journey, panel) have no machine-checkable anchor | G5 | P2 | TM | open |
| B21 | Assessment coverage: which objectives/techniques/controls are taught but never assessed? | G6 | P1 | RB | open (Stage 6) |

## Notes
- Several 2026-10-30 board items are now **closed** by prior work (CI exists; phone-bridge consent fixed;
  measurable objectives added; CWE-121→787/788 corrected in the Threat Map). Re-verify and prune during
  Stage 2.
- B01/B02 land with PR #12; the rest are worked in their stages. Each fix PR updates this row's Status.
