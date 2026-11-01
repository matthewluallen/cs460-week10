# Chain card — Trust chain: untrusted code reaches the controller

> Stage 3, chain #3. Merges into the Signal Journey at **link 4** (code). Two paths, same consequence.
> Parts 6 (unsigned OTA), 7 (supply-chain payload), 9 + Week 12 (the fix).

- **Consequence:** the code that runs the scan is not the audited code → it can move RELAY 1 with no field
  event and no audited row.
- **Status:** draft.

## Path A — unsigned OTA (Part 6)
| # | From → To | Lens | Establishes | Does NOT establish | Technique | CWE | Obs? |
|---|---|---|---|---|---|---|---|
| A1 | attacker → :80 `/api/upload`, cleartext, no login | Wire | the update service is reachable + open | that an image will boot | T1693.001 System Firmware | CWE-306/319 | yes |
| A2 | push unsigned blob → `verified=NO`, **stored** | Code | the device accepts unsigned bytes | that it will execute (boot-loader format check rejects 22 junk bytes) | T1693.001 | **CWE-345/347** (no integrity/sig check) | yes (reply + log) |
| A3 | elf_sha256 unchanged | Code | the malformed blob never ran | that a *well-formed* malicious image wouldn't (→ Path B) | — | — | yes |

## Path B — supply-chain dormant payload (Part 7)
| # | From → To | Lens | Establishes | Does NOT establish | Technique | CWE | Obs? |
|---|---|---|---|---|---|---|---|
| B1 | trojaned image (one compile flag) boots; beacons to C2 | Code · C2 | a well-formed image runs different code | where the code **came from** — the shipped file can't prove provenance | T0862 Supply Chain Compromise | CWE-506 Embedded Malicious Code | yes (C2 log) |
| B2 | diff clean vs trojaned → ~1.67 M bytes differ (same size) | offline | a byte-diff **cannot** isolate the ~624-byte payload | anything about origin — only a reproducible/signed build can | — | — | yes |
| B3 | C2 says "armed" → relay0 closes, IN1 open, no operator cmd | Code → Physical | actuation with **no cause at link 1** and **no audited row** | that an inbound firewall would see it (it rides the outbound check-in) | **T0835** · T0831 | — | yes (/field, tap) |

## Bowtie — barriers
- **Signed firmware** (D3-DENCR / code-signing) — vulnerable: none; secure: HMAC (**key is a repo
  constant — residual risk**); Week 12: public-key signatures.
- **Secure boot + flash encryption** — **absent on this board** (named as residual risk).
- **Provenance: reproducible build + SLSA / in-toto attestation** — the correct answer to "where did it
  come from"; taught in prose (Part 7), not demonstrated.
- **Egress filtering / no outbound C2** (IEC 62443 FR5; D3-OTF outbound-traffic filtering) — the beacon is
  the detection/prevention point.

## Residual risk
"Even if someone can reach the update path, RELAY 1 stays bounded by ___" — on the secure build, by an
HMAC whose key ships in every image; Week 12 replaces it with public-key signatures + a build record.

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| B13 | **Neither path has a Threat-Map row** (unsigned OTA T1693.001; supply-chain T0862). | G8 | Stage 5: add both. | P1 |
| F1 | CWE for A2 is the integrity-check gap — pin it to **CWE-347** (improper verification of signature) + CWE-345, not just "accepts bytes". | G2 | Stage 5 | P2 |
| F2 | SLSA/in-toto + secure-boot are **prose-only**; no artifact demonstrates provenance. | G9 | note as deliberately conceptual; or add a signed-build demo in Week 12. | P2 |
| F3 | Egress filtering as a compensating control isn't named (the C2 beacon is the hook). | G4 | add to the Part 7/9 control story. | P2 |
