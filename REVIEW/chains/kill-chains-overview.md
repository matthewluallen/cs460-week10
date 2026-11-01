# Chain card — Kill-chain overview (the 9 incident cases × ATT&CK-ICS stages)

> Stage 3, chain #7. Each lab Part opens with a real incident; this maps them to the ATT&CK-for-ICS
> tactics (stages) they illustrate, the technique, and the trust/signal chain(s) they exercise.
> Purpose: see which kill-chain **stages** the lab covers — and which it doesn't.

## Incidents × stage
| Part | Incident (ATT&CK ID) | Primary ICS stage(s) | Technique | Exercises chain |
|---|---|---|---|---|
| 1 | TRITON 2017 (S1009) | Inhibit Response Function · Impact | T0880 Loss of Safety · M0805 (defense) | signal-journey (links 3/6: safety is only an input) |
| 2 | Stuxnet 2010 (S0603) | Impair Process Control · Evasion | T0889 Modify Program · T0832 Manipulation of View | signal-journey (link 3) |
| 3 | FrostyGoop 2024 (S1165) | Impair Process Control · Impact | T1692.001 · T0836 Modify Parameter | trust-network-write |
| 4 | Relabeled patient monitor 2025 (ICSMA-25-030) | Initial Access · Persistence | T0862 Supply Chain | trust-firmware (hardware analog) |
| 5 | Ripple20 2020 (ICSA-20-168) | Execution · Initial Access | T0866 Exploitation of Remote Services | signal-journey (link 4) / SBOM reachability |
| 6 | Ukraine grid 2015 (Sandworm G0034) | Inhibit Response Function · Impact | T1693.001 System Firmware · T1695.001 Block Serial Comms | trust-firmware (Path A) |
| 7 | Havex / Dragonfly 2013–14 (S0093/G0035) | Collection · Command & Control | T0862 Supply Chain | trust-firmware (Path B) |
| 8 | Y2038 tank-gauge 2025 (ICSA-25-296) | Evasion · Denial of View | T0830 AiTM · T1692.002 · T0815 | trust-time · trust-sensor |
| 9 | CyberAv3ngers 2023 (G1027) | Initial Access · Impair Process Control | T1694.001 Default Credentials | (hardening — refutes all) |

## ATT&CK-ICS stage coverage (what the lab teaches vs gaps)
| Stage | Covered? | Where |
|---|---|---|
| Initial Access | yes | P4/P5/P6/P9 (supply chain, remote svc, OTA, default creds) |
| Execution | yes | P5/P7 (exploitation, trojaned image) |
| Persistence | partial | P4/P7 (implied; not demonstrated on the twin) |
| Privilege Escalation | **no** | — (G8: no priv-esc technique; defensible for a device lab) |
| Evasion | yes | P2/P8 (manipulation/denial of view) + the overflow audit-bypass |
| Discovery | partial | P3/P4 (i2cscan, bus enum) as recon — not framed as ATT&CK Discovery |
| Lateral Movement | **no** | — (G8: the engineering station → PLC pivot isn't shown; ties to the ATT&CK **Enterprise** gap B17) |
| Collection | yes | P7 (Havex mapped the plant) |
| Command & Control | yes | P7 (the C2 beacon) |
| Inhibit Response Function | yes | P1/P6 (safety defeat, comms block) |
| Impair Process Control | yes | P1/P2/P3/P9 (relay/param/program) |
| Impact | yes | the consequence of every chain |

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| K1 | **Lateral Movement is absent** — the engineering-station → PLC pivot (how the attacker *gets* on the plant LAN) is assumed, not shown; the IT side is unmapped (ties to **B17**, ATT&CK Enterprise). | G8/G1 | Stage 5: map the HMI/engineer station to ATT&CK Enterprise; optionally add a pivot step. | P2 |
| K2 | **Discovery** (i2cscan, bus enum, SBOM) is recon but isn't framed as the ATT&CK Discovery stage. | G8 | Stage 5: tag the recon steps. | P3 |
| K3 | **Persistence** is implied (firmware/supply-chain) but never demonstrated surviving a reboot on the twin. | G9 | note as conceptual, or add a reboot-survival check. | P3 |
| K4 | The incidents are cited in the lab "Sources" but most are **not** in the Threat-Map/objectives cross-walk as kill-chain stages — a third consistency surface (ties to **B15**). | G7 | Stage 5: reconcile. | P2 |
