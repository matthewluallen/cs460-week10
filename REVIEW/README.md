# REVIEW — staged gap-review of the whole project

A living, in-repo review of the entire lab (repo code, lab steps, assignments) for **gaps** — in the
individual items, in how they **join into risks** (data point → data point), in their **compensating
controls**, and in their mapping to **MITRE and other security taxonomies/methods**.

The full staged plan lives in the team project doc `module5-gap-review-staged-plan-2026-10-31`. This
folder is the working copy that travels with the code so CI can check it and every change can update it.

## Files
- `README.md` — this charter: the spine, the gap taxonomy, the working rhythm.
- `templates/item-card.md` · `templates/chain-card.md` · `templates/control-card.md` — the three review
  units. Copy one per item/chain/control into `items/`, `chains/`, `controls/` as we work each slice.
- `01-inventory.md` — the master index of every reviewable item (Stage 1).
- `02-coverage-matrix.md` — item × taxonomy and ATT&CK-ICS technique × item (Stage 1); blanks are
  candidate gaps.
- `backlog.md` — the single running, prioritized gap backlog (Stage 7, fed continuously).

## The spine — what every item and chain gets mapped to
One fixed set of lenses, each answering a different question. A gap is usually a missing answer.

| Lens | Question | Framework(s) | In repo today |
|---|---|---|---|
| Device property | What makes the device vulnerable? | MITRE **EMB3D** | yes (Threat Map) |
| Weakness | What code/design flaw? | **CWE** | yes |
| Known instance | A specific published bug? | **CVE** + **CVSS/EPSS/KEV** + **VEX** | CVE/EPSS/KEV in P5; VEX no |
| Attack pattern | How, in the abstract? | **CAPEC** | yes |
| Technique (OT) | What the adversary does on ICS? | **ATT&CK for ICS** | yes |
| Technique (IT) | …on the IT side (HMI container, workstation)? | **ATT&CK Enterprise** | **no** |
| Emulation | How a red team runs it? | **CALDERA / caldera-ot** | partial |
| Countermeasure | What defense stops/detects it? | **D3FEND** | yes |
| Control (standard) | Required control + security level? | **ISA/IEC 62443** (FR/SL), **NIST 800-82/800-53/CSF** | **no** |
| Engineering stance | Which CIE principle governs trust? | **CIE / CCE** | yes (objectives) |
| AI/ML surface | Any ML/data-poisoning surface? | **ATLAS** | N/A today (Threat Map notes the hook) |
| Supply chain | How is provenance established? | **SLSA / in-toto** | referenced (P7) |
| Threat model (per flow) | What can go wrong on this flow? | **STRIDE** | **no** |
| Chain reasoning | How links combine; where barriers sit? | **attack trees / bowtie**, ATT&CK-ICS kill-chain | implicit |
| Consequence | Worst physical outcome? | **CCE / FMEA** | implicit |
| Prioritization | What to fix first? | **SSVC** (or CVSS+EPSS+KEV+consequence) | **no** |

The **no** rows are deliberate Stage-5 decisions — we adopt a framework only where it adds rigor for a
teaching lab, not reflexively.

## Gap taxonomy (every finding gets one type)
- **G1** missing item · **G2** incorrect mapping · **G3** broken/implicit join (unobservable link) ·
  **G4** missing compensating control · **G5** untested (no machine anchor) · **G6** unassessed ·
  **G7** inconsistency/drift across sources of truth · **G8** unmapped to an in-scope taxonomy ·
  **G9** overclaim (mapped/asserted but not demonstrable).

## Three sources of truth (kept in sync — drift is G7)
1. `assessment/objectives.md` — objectives + ATT&CK/CWE/CIE tags.
2. `web/public/threat-map.html` — the EMB3D→CWE→CAPEC→ATT&CK→CALDERA→D3FEND→ATLAS cross-walk.
3. The lab text (Canvas page + each `*/README.md`) — the per-part "Sources:" lines.

## Working rhythm (incremental + collective)
Each stage is worked in slices. For a slice: a card is pre-filled with a proposed review + gaps + fix →
reviewed and decided → agreed fixes land as a small PR → the backlog + matrices are updated. Nothing is
settled until signed off. See the plan doc for the full stage list (0 Method · 1 Inventory · 2 Nodes ·
3 Chains · 4 Risk+controls · 5 Cross-walk · 6 Assessment · 7 Synthesis/living).
