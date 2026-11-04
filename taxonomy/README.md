# Taxonomy & standards association layer

A reusable, drift-proof way to bind platform content (control points, embedded weaknesses,
attack steps, safety functions) to **canonical identifiers from authoritative frameworks** —
so every claim connects to the real security and automation taxonomies, and CI fails if a
reference goes stale.

- **Spec:** [`SCHEMA.md`](SCHEMA.md) — the `tbd.taxonomy-association/v1` data model other modules adopt.
- **Registry:** [`registry.json`](registry.json) — every framework, its authority, license, and whether it is *vendored* (pinned snapshot) or *reference-only* (copyrighted IEC/ISO, cited by number+clause, never reproduced).
- **Vendored snapshots:** [`vendor/`](vendor/) — pinned, distilled, hash-stamped extracts of the open frameworks.
- **Crosswalks:** [`crosswalks/`](crosswalks/) — one file per content artifact; drop a new one in and CI validates it.
- **Validate:** `python3 taxonomy/tools/validate.py` — checks every ID against the pinned snapshots; a revoked or unknown ID fails the build.
- **Update (deliberate):** `taxonomy/tools/refresh.sh` re-pulls upstream and re-pins; review the version/sha diff and commit.

## Attribution
This layer references **MITRE ATT&CK®, ATLAS™, CWE™, CAPEC™, and D3FEND™** — © The MITRE
Corporation, used with attribution; MITRE does not endorse this work. **NIST** material is a U.S.
Government work (public domain). **IEC and ISO** standards are copyrighted and paywalled: they are
cited by standard number, edition, and clause only — none of their text is reproduced here.
