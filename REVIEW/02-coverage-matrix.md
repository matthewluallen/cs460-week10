# 02 — Coverage matrices (Stage 1)

Seeded from the three sources of truth at merge time. Blanks and ⚠ are candidate gaps to work in later
stages. `—` = the framework entry is intentionally null/N-A in the source; **(blank)** = not mapped.

## Matrix A — the 8 Threat-Map attacks × taxonomy
(from `web/public/threat-map.html`, `ATTACKS[]`)

| # | Attack (Part) | EMB3D | CWE | CAPEC | ATT&CK ICS | CALDERA | D3FEND | ATLAS |
|---|---|---|---|---|---|---|---|---|
| 1 | Default/hardcoded web creds (P5) | TID-311 | 798, 1392 | CAPEC-70 | T1694.001 | — | D3-MFA | — |
| 2 | Hardcoded crypto key (P5) | TID-317 | 321 | CAPEC-37 | — | — | D3-DENCR | — |
| 3 | Unauthenticated Modbus/TCP control (P3) | TID-310 | 306 | CAPEC-1 | T1692.001, T0835 | modbus | D3-MAN | — |
| 4 | Modbus command injection/spoof (P3) | TID-406, 407 | 345, 940 | CAPEC-594 | T1692.002 | modbus | D3-MAN | — |
| 5 | MQTT DCMD → coil (no broker auth) (P3) | TID-310 | 306, 862 | CAPEC-1 | T1692.001 | — | D3-MAN | — |
| 6 | Buffer overflow → phantom relay force (P6/7) | TID-327 | 787, 788 | CAPEC-100 | **T0866 ⚠** | — | D3-SFCV | — |
| 7 | Firmware extraction & RE (P5) | TID-115, 108 | 312, 1191 | CAPEC-37, 167 | T0845 | — | D3-DENCR | — |
| 8 | Unauth phone-sensor → PLC input (P8) | TID-406, 310 | 345, 306 | CAPEC-594 | T1692.001, T0835 | — | D3-MAN | — |

**Lenses absent from every row (Stage-5 adopt-decisions, all G8):** ISA/IEC 62443 (FR/SL), NIST
800-82/800-53/CSF, STRIDE, ATT&CK **Enterprise** (the HMI container / engineering station are IT, not
ICS), SSVC/VEX, SLSA/in-toto (referenced in P7 prose but not in this matrix), CIE principle per attack.

**⚠ G2 (known):** attack 6 maps to T0866 (Exploitation of Remote Services) but the *effect* is I/O-image
manipulation — should be **T0835** (and keep T0866 only for the remote-reach step). Flagged by the
2026-10-30 board; still present.

## Matrix B — ATT&CK-ICS techniques referenced in the lab "Sources" lines × Part
(from the Canvas lab page per-part Sources; "TM?" = also in the Threat-Map cross-walk)

| Technique | What | Part(s) | Demonstrated vs cited | TM? |
|---|---|---|---|---|
| T0880 Loss of Safety | safety function defeated | P1 | cited (TRITON case) | no |
| M0805 Mechanical Protection | fail-safe as a layer | P1 | demonstrated (E-stop) | no |
| T0889 Modify Program | change the ladder | P2 | demonstrated | no |
| T0843 Program Download | load to controller | P2 | demonstrated | no |
| T0832 Manipulation of View | falsified operator view | P2 | cited (Stuxnet) | no |
| T0836 Modify Parameter | change a setpoint | P3, P9 | demonstrated (overlong tag) | partial |
| T1692.001 (was T0855) Unauthorized Command | command w/o auth | P3 | demonstrated | **yes** |
| T0835 Manipulate I/O Image | write the output image | P3, P8 | demonstrated | **yes** |
| T0862 Supply Chain Compromise | trojaned image/parts | P4, P7 | demonstrated (P7) | no |
| T0866 Exploitation of Remote Services | reach a service | P5 | demonstrated | **yes (⚠ mislabel)** |
| T1694.002 (was T0891) Hardcoded Creds | creds in image | P5 | demonstrated (recovered) | yes (as T1694.001 on #1) |
| T1693.001 (was T0857) System Firmware | overwrite firmware | P6 | demonstrated (unsigned OTA) | no |
| T1695.001 Block Comms: Serial | brick comms | P6 | cited (Ukraine 2015) | no |
| T1692.002 (was T0856) Reporting Message | spoof a reading | P8 | demonstrated | **yes** |
| T0830 Adversary-in-the-Middle | on-path (NTP) | P8 | demonstrated (rogue NTP) | no |
| T0815 Denial of View | hide state | P8 | partial | no |
| T1694.001 (was T0812) Default Creds | factory creds | P9 | demonstrated (refused) | **yes** |
| T0838 ...(credential/parameter) | — | P9 | — | no |

### Immediate Stage-1 findings (seed the backlog)
1. **G8 — three headline attacks are not in the Threat-Map cross-walk:** the **unsigned OTA** (P6,
   T1693.001), the **supply-chain payload** (P7, T0862), and **NTP/Y2038** (P8, T0830 + CWE-197). They
   appear in the lab Sources and objectives but have no Threat-Map row. (Overflow-force is in the map and
   tagged "P6/7", but the OTA *acceptance* and the *payload detonation* are distinct attacks.)
2. **G7 — the three sources of truth diverge:** the lab "Sources" lines reference ~18 techniques; the
   Threat-Map maps 8 attacks (6 distinct techniques); objectives.md tags a third set. A technique can be
   labeled or omitted differently in each. Stage 5 reconciles them and adds a CI consistency check.
3. **G2 — T0866 → should be T0835** on the overflow-force row (carried from the 2026-10-30 board).
4. **G8 — no control-standard lens anywhere:** nothing maps to IEC 62443 or NIST 800-82/800-53, so the
   compensating-control story (Stage 4) has no standard backbone yet.
5. **G9 — CWE-78** (command injection) is cited but has no shell on the ESP32 — pattern-only; must not sit
   in an "observable" column.
