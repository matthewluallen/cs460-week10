# 06 — Assessment alignment (Stage 6)

Objective → activity → assessment, and what the assessment does and does **not** score. The headline:
**assessment is strong and in-repo** — much more mature than the 2026-10-30 board found (`assessment/rubric.md`
exists, is reasoning-weighted 40/60, and Part 9 already requires residual limits; `week12/rubric.md` adds a
*residual-risk rule*). So Stage 6 is about **specific coverage holes**, not a missing rubric.

## What's in place (the baseline)
- `assessment/objectives.md` — measurable objectives, each tied to a verifier anchor + a rubric.
- `assessment/rubric.md` — **40% mechanics** (`verify-lab.py`) + **60% reasoning** graded on the four-element
  finding standard (Shows / Does-not-establish / Uncertainty / Next-observation, 0–3 each). Per-part + E1–E3
  rows. SKIP transfers mechanics→reasoning (handles Part 7 gracefully). Twin↔physical transfer bonus.
- `week12/rubric.md` — 35/65, with the **residual-risk rule**: a compensating control submitted *without a
  stated residual risk* earns no reasoning credit. This is the model.
- Machine anchors: `verify-lab.py P1–P9`, `verify-secure.py`, `verify_controls.py` (Z3).

## Alignment matrix (per part)
| Part | Objective | Mechanics anchor | Anchor health | Reasoning scored? |
|---|---|---|---|---|
| 1 | fail-safe motor | `P1` | OK | yes (finding std) |
| 2 | prove installed program | `P2` + studio-regression | OK | yes |
| 3 | decode one exchange | `P3` | OK | yes |
| 4 | HBOM vs bus | `P4` | OK | yes |
| 5 | SBOM reachability | `P5` | OK | yes |
| 6 | unsigned OTA | `P6` | OK | yes |
| 7 | supply-chain payload | `P7` | **SKIP — no anchor for the no-input relay move (B11)** | yes (reasoning-weighted) |
| 8 | NTP/Y2038 + phone FDI | `P8` | **time half OK; phone-FDI half has no anchor (CS1)** | yes |
| 9 | harden + residual | `P9`, `verify-secure` | **positive control broken — B07** | yes (fix **+ residual** required) |
| E1 | provenance gap | `/api/log` line → source | OK | yes |
| E2 | signal journey (5 lenses) | **— (none)** | reasoning-only | yes (per-lens can/can't) |
| E3 | phone FDI + plausibility gate | **— (instructor-judged, CS1)** | no machine anchor | yes (gate + legit passes) |
| W12 | formal proofs | `verify_controls.py` | OK | yes (residual-risk rule) |

## Gaps (typed)
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| A1 | **Chain/cross-layer reasoning isn't its own scored dimension.** The joins (what a read establishes about the *next* layer) are the course spine, but they're only implicit in per-node "Does not establish" and in the optional E2. | G6 | add a "cross-layer / chain" reasoning element (or make the Signal-Journey finding a required, scored core deliverable). | P1 |
| A2 | **Compensating-control depth isn't required.** Part 9 scores the *primary* fix + residual (good), but not the defense-in-depth / compensating controls or the 62443/NIST control-standard mapping that Stage 4 surfaced. Week 12 goes deeper. | G6 | port Week 12's residual-risk-rule + a "compensating control + 62443/NIST" ask into the main Part 9 rubric. | P1 |
| A3 | **The multi-taxonomy walk isn't a scored dimension.** Part 9 walks the Threat-Map row (now incl. 62443/NIST), but mapping quality isn't a rubric element. | G6 | add a one-line taxonomy-mapping check to the Part 9 reasoning. | P2 |
| A4 | **Three anchor holes:** E3 plausibility gate (CS1), P7 no-input relay move (B11), P9 positive control (B07). The rubric tolerates SKIP, but these are the machine-check gaps. | G5 | add/repair the three anchors. | P1 |
| A5 | **The summative artifact's rubric is Canvas-only.** The graded deliverable is the 5–8 min video, but no video rubric is in-repo — only the per-part finding rubric is. The actual grade-producing artifact isn't version-controlled. | G6/G7 | bring the video rubric into `assessment/` (reuse the finding standard). | P2 |
| A6 | **Not taught → not assessed:** Lateral Movement / the IT side (ATT&CK Enterprise, B17/CK1) is neither taught nor assessed; a scoped pivot would close both. | G8 | Stage 5/6 follow-up. | P3 |

## Recommendations (for Stage 7)
1. Make the **Signal-Journey chain finding a required, scored core deliverable** (closes A1 + raises the
   chain reasoning the whole review is about).
2. **Port Week 12's residual-risk rule + a compensating-control/62443 ask into the main Part 9 rubric** (A2).
3. **Close the three anchor holes** (A4): a verifier for the E3 gate, a scripted P7 assertion, and the B07
   secure-boot fix — then every part has a working mechanics anchor.
4. **Bring the video rubric in-repo** (A5), reusing the finding standard, so the summative grade is
   version-controlled like everything else.
