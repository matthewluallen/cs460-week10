# Chain card — Trust chain: network write → coil (unauth Modbus / MQTT)

> Stage 3, chain #2. Merges into the Signal Journey at **link 5** (output image), bypassing links 1–4.
> Parts 3 (both paths), 9 (the fix). Firmware: `modbus_tcp_server.cpp` (`fc_write_single_coil`).

- **Consequence:** an unauthenticated client on the plant subnet sets RELAY 1 — no field event, no program change.
- **Status:** draft.

## Links
| # | From → To | Lens | Establishes | Does NOT establish | Technique | CWE | Obs? |
|---|---|---|---|---|---|---|---|
| 1 | attacker reachable on 10.20.30.0/24 | Wire | a path to :502 / :1883 exists | authorization — the services answer anyone | T0883 Internet-Accessible Device (analog: flat plant LAN) | CWE-668 | yes (tshark) |
| 2 | crafts Modbus FC5 (coil 1=relay0) **or** MQTT Sparkplug DCMD | Wire | a well-formed command can be built by anyone | that the sender is the HMI/engineer | T1692.001 Unauthorized Command | CWE-306/862 | yes |
| 3 | PLC accepts it (no auth) → writes the coil | Code → Bus · `fc_write_single_coil` | the write reaches the **output image** | nothing about intent or identity — Modbus/MQTT carry none | **T0835** Manipulate I/O Image | CWE-306 | yes (the coil read-back changes) |
| 4 | output image → RELAY 1 | Physical | actuation | contact-moved vs `act` latch | T0831 | — | yes |

## Bowtie — barriers
- **Message authentication** (D3-MAN) — Modbus has none; the secure build requires an authenticated
  REMOTE-mode prerequisite for writes (not full client auth — *residual risk*).
- **Zones & conduits / segmentation** (IEC 62443 FR1/FR5) — the flat plant LAN is the enabling condition;
  a conduit with an allow-listed Modbus client is the compensating control.
- **Out-of-band monitoring** — detect an unexpected writer (the capture station is the teaching analog).

## Residual risk
"Even if the network is on the same LAN, RELAY 1 stays bounded by ___" — currently **nothing** on the
vulnerable build; on the secure build only by the REMOTE-mode gate (which does not authenticate the client,
only the mode). **This is a single-point barrier (G4).**

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| B13 | Not a Threat-Map row of its own (folded under "Unauthenticated Modbus control") — the **MQTT DCMD→coil** path (TID-310/CWE-306+862) has a row; confirm the Modbus FC5 write path is distinctly mapped. | G8 | Stage 5 | P1 |
| N1 | No **62443 FR/SL** or segmentation control is named as the compensating control in the lab — the fix is framed as message-auth only. | G4 | add zones/conduits to the Part 9 control story | P2 |
| N2 | Is the MQTT **DCMD→coil** write actually exercised end-to-end on the twin, or only phone→point? (the firmware subscribes; the lab demonstrates publish, not a coil DCMD). | G9 | verify on twin; label if not demonstrable | P2 |
