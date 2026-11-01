# Chain card — <CHAIN NAME>

> How data points join into a risk (Stage 3). Copy to `REVIEW/chains/<id>.md`.
> A chain is an ordered path of observations/links from a source to a physical/operational consequence.

- **ID / name:**
- **Type:** signal journey · trust chain · kill chain (incident: <case>)
- **Consequence (worst outcome):** <the physical/operational effect at the end>
- **Owner / reviewer:** · **Status:** draft / reviewed / fixed / signed-off

## The links (data point → data point)
For each hop: the observation, the instrument/lens, what it **establishes** about the next layer, what it
**does not** establish, and the per-link taxonomy.

| # | From → To | Lens / instrument | Establishes | Does NOT establish | Technique | CWE | CAPEC | EMB3D | Kill-chain stage | Observable? |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | | | | | | | | | | |
| 2 | | | | | | | | | | |

## Attack-tree / bowtie view
<the path as an attack tree (AND/OR of links) with the barriers (controls) placed on each edge; the
"bowtie" center is the top event = the consequence, left = causes, right = mitigations/consequences>

## Gaps (typed) — focus on the JOINS, not the nodes
| # | Gap | Type (G1–G9) | Which link / join | Proposed fix | Priority |
|---|---|---|---|---|---|

## Linked cards
- Item cards for each node: <links>
- Control cards for each barrier: <links>
