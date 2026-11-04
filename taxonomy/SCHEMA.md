# `tbd.taxonomy-association/v1` — the platform association schema

A small, reusable contract any module or course adopts to connect its content to authoritative
frameworks with integrity. One crosswalk file per content artifact lives in `crosswalks/`; CI
validates it against the registry and the pinned vendored snapshots on every push.

## Object model
A crosswalk is `{schema_version, content, title?, objects[]}`. Each **object** is one thing in the
content that deserves an authoritative association:

| field | meaning |
|---|---|
| `id` | local id, `kind:slug` (e.g. `point:motor`, `attack:spoof-command`) |
| `kind` | `point` \| `weakness` \| `attack_step` \| `safety_function` \| `mitigation` \| `control` |
| `title` | human label |
| `automation` | optional: `iec_61131_3_address` (`%QX0.0`), `iec_81346_2_designation` (`-K1`), `iec_81346_2_class` (`K`), `engineering_unit` |
| `refs[]` | taxonomy references (below) |
| `notes` | free text |

## Reference model
Two kinds, both declared in [`registry.json`](registry.json):

- **Vendored framework** (MITRE ATT&CK/ATLAS/CWE/CAPEC/D3FEND, NIST CSF): `ref = {framework, id, relation?}`.
  The `id` is matched against the framework's `id_pattern` and **must exist and be ACTIVE** in the
  pinned snapshot under `vendor/`. A revoked or deprecated id fails the build and the error names
  the replacement — references cannot silently rot.
- **Reference-only standard** (copyrighted IEC/ISO, NIST SP docs): `ref = {framework, clause|section|part|requirement, title}`.
  The registry lists the required `citation_fields`; the validator checks they are present. Only the
  canonical identifier, edition, clause and the public title are stored — never the standard's text.

`relation` captures the association kind: `is`, `exploits`, `enables`, `mitigated_by`,
`detected_by`, `realizes`, `conforms_to`.

## Integrity model
- **Pinned + reproducible.** Each `vendor/*.json` snapshot carries its upstream URL, version, and
  the SHA-256 of the exact source bytes it was distilled from. Builds are offline and deterministic.
- **CI-validated.** `tools/validate.py` runs in CI; unknown, revoked, or malformed references fail.
- **Deliberate updates.** `tools/refresh.sh` re-pulls upstream, re-distills, and re-pins; a
  maintainer reviews the version/SHA diff and commits. (This is the opt-in "track latest" path.)

## Adoption (other modules)
1. Add any new frameworks you need to `registry.json` (vendored → add a distiller + snapshot; reference-only → declare `citation_fields`).
2. Write `crosswalks/<artifact>.json` to `tbd.taxonomy-association/v1`.
3. `python3 taxonomy/tools/validate.py` locally; CI enforces it thereafter.

## Provenance of this layer
Frameworks, licenses, and attribution are in [`registry.json`](registry.json) and
[`README.md`](README.md). MITRE/NIST data is vendored with attribution; IEC/ISO are cited by
reference only.
