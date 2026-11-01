# 07 — Synthesis & living cadence (Stage 7)

The whole review in one place: the verdict, the prioritized remediation plan, quick-wins vs. structural
work, and how the review stays current.

## Verdict
The project's **security substance is strong and mature** — the content is accurate to a professional
standard, the observation-vs-inference discipline is the real curriculum, and after Stage 5 the taxonomy
cross-walk is internally consistent. The review did **not** find the content wanting; it found that the
project's own strongest ideas are **under-connected and under-assessed**:

1. **The chains are taught but not scored.** The data-point-to-data-point reasoning (the Signal Journey and
   the trust/kill chains) is the spine of the course, yet it's assessed only implicitly and in optional
   extensions (A1). This is the single highest-leverage gap.
2. **Compensating controls are named, not demonstrated or required.** Most risks have a primary control;
   the defense-in-depth story (62443 zones/conduits, SLSA/in-toto, egress filtering, out-of-band
   cross-checks) lives in prose, and the main-lab rubric asks for the primary fix + residual but not the
   depth Week 12 already models (A2, CN1, CF2/CF3).
3. **A few real bugs/holes** block or weaken specific proofs: the secure build's positive control can't run
   (B07), Part 7 has no automated proof (B11), the E3 gate has no anchor (CS1).
4. **The terminal barrier is deliberately absent** (an un-rewritable protection) — the TRITON/CIE capstone.
   Correct pedagogy; it should be *stated as a design stance*, not left to read as a gap (CC6a).

## Prioritized remediation plan

### Already landed / in a PR
- **PR #12** — B01 (verify-lab P1), B02 (phone-card registers).
- **PR #14** — B03–B06, B08–B10, B12 (lab-text + Studio fixes; B05/B12 need a container/stack run).
- **PR #15** — B13 (3 Threat-Map rows), B14 (T0835), B16 (62443/NIST column), CO1 (D3FEND bounds-check),
  CO2 (CWE-778), CF1 (CWE-347), CT2 (CWE-190/191), B15 (sources-of-truth note).

### P0 — correctness, before the next class
| Item | What | Why now | Effort |
|---|---|---|---|
| **B07** | secure build boots without activating the seeded motor program → Part 9 positive control fails | Part 9's "prove the fix" can't fully run | structural (firmware boot-load or verify-secure load) |
| **B03/B09 (Canvas)** | the jog-text correction + the 8090-public note are in the repo copy but **the Canvas page still needs them** | student-facing, wrong-answer risk | quick (Canvas edit) |

### P1 — high value
| Item | What | Effort |
|---|---|---|
| **A1** | make the Signal-Journey chain finding a **required, scored** deliverable (score cross-layer reasoning) | structural (rubric + lab) |
| **A2** | port Week 12's residual-risk rule + a **compensating-control / 62443** ask into the main Part 9 rubric | quick (rubric) |
| **A4 / CS1 / B11** | close the three anchor holes: a verifier for the E3 gate, a scripted P7 assertion, the B07 fix | structural |
| **CN1** | name/demonstrate **62443 zones/conduits (FR5)** as the network compensating control (not msg-auth only) | medium (a segmented-plant variant) |
| **C3a / G3-b / G3-c** | the chain **joins** (logic-solve is inferred; forge-points + unaudited write not on the journey) | quick-medium (label + 2 journey branches) |

### P2 — depth & reach
B17/CK1 (ATT&CK **Enterprise** + a scoped Lateral-Movement pivot) · CF2/CF3/CS3 (demonstrate the prose-only
compensating controls: SLSA/in-toto, egress filtering, out-of-band cross-check) · CT1 (a time-gated control
decision so the time→logic join is shown) · A5 (bring the **video rubric** in-repo) · A3 (score the
taxonomy walk) · CC6a/CC6b (state the un-rewritable-protection stance + a reference bounding layer).

### P3 — polish
C3c (`act`≠contact caveat everywhere) · CK2 (frame recon as Discovery) · CT2-doc · CC-cosmetics · the
2026-10-30 board's remaining a11y items (B19 panel.html) — re-verify/prune in a pass.

## Quick wins vs. structural
- **Quick wins (text/rubric/label, no firmware):** A2, A3, A5, CN1-doc, C3a labels, CC6a, the Canvas edits,
  the board a11y prune. A single "rubric + lab-text" PR could land most of P1's cheap half.
- **Structural (firmware / design / new artifacts):** B07 (boot activation), A1 (scored chain deliverable +
  its anchor), A4 anchors (E3 verifier, P7 assertion), CN1 segmented-plant variant, the "demonstrate the
  compensating control" items (CF2/CF3/CS3), CT1 time-gated decision.

## The living cadence (how this stays true)
1. **One authoritative mapping:** `web/public/threat-map.html` is the cross-walk of record; `REVIEW/` is the
   audit trail; `objectives.md` and the lab "Sources" must agree with it.
2. **A CI consistency check (B15 follow-on):** a small script that parses the technique/CWE IDs out of
   objectives.md, threat-map.html, and the lab Sources and **fails CI on drift** — so the three sources
   can't silently diverge again (the root cause of the T0866 and register bugs).
3. **Re-run the affected slice on change:** a PR-template checkbox — *"touches a Part / firmware / mapping?
   update its item/chain/control card + the backlog row."*
4. **Owner + trigger:** the backlog is the single source for "what's left"; each fix PR flips its row's
   Status; `00-index`/matrices are updated when an item/mapping is added.

## Where the review stands
Stages 0,1,3,4,5,6,7 complete. The analysis is done end-to-end; execution now runs off the P0→P3 plan above,
landed as small PRs that each update a backlog row. The review is designed to be re-entered a slice at a
time, not redone.
