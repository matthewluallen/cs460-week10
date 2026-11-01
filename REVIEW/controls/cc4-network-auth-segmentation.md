# Control card — CC4 · Network authentication + segmentation

- **Risk / chain:** an unauthenticated client on the plant LAN writes the coil (trust-network-write:
  Modbus FC5 / MQTT DCMD), or a trojan beacons to C2 (trust-firmware Path B). Consequence: relay0 set, or
  outbound control, with no identity check.
- **Status:** draft.

## Primary mitigation
- **Control:** **message authentication** — Modbus/MQTT carry no identity, so the fix is to authenticate
  the sender; the secure build gates writes behind an authenticated **REMOTE** mode. · **Design.**
- **Where:** Part 3 (the unauth attack), Part 9 (`verify-secure`: the REMOTE-mode write prerequisite).
- **D3FEND:** D3-MAN (message auth), D3-NTA (network traffic analysis), D3-OTF (outbound filtering) ·
  **62443:** **FR5 RDF (zones & conduits — the core ICS control)**, FR1 IAC, FR2 UC · **NIST:** AC-4
  (information-flow enforcement), SC-7 (boundary protection), AC-3 (access enforcement), SI-4 (monitoring)
  · **CIE:** *a message arriving intact says nothing about who sent it.*

## Compensating / defense-in-depth controls
| Control | Prevent or detect? | D3FEND / 62443 | What it still trusts |
|---|---|---|---|
| **Zones & conduits / segmentation** (the real ICS control) | prevent | FR5 / SC-7 | the segmentation being enforced |
| Allow-listed Modbus/MQTT clients | prevent | FR1 / AC-3 | the allow-list integrity |
| Egress filtering (block the C2 beacon) | prevent + detect | D3-OTF / FR5 | the boundary device |
| Network monitoring (the capture station) | detect | D3-NTA / SI-4 | someone watching |

## Single points of failure & residual risk
- **SPOF:** the secure REMOTE-mode gate is **not client authentication** — it authenticates the *mode*, not
  the *sender*. On a flat plant LAN, segmentation (the actual primary ICS control) is **absent**.
- **Residual:** "On the same LAN, the coil is bounded only by the REMOTE-mode gate — which any mode-change
  can satisfy." The missing primary is **FR5 zones/conduits**.

## Does the lab teach AND assess both?
- **Taught:** Part 3 (attack) + Part 9 (REMOTE gate). The stations plant *demonstrates* a flat subnet.
- **Gap (CN1):** the lab frames the fix as **message-auth only** and never names/demonstrates
  **segmentation (62443 FR5)** as the compensating control — the single biggest missing control-standard
  tie-in (backlog B16).

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| CN1 | no 62443 zones/conduits or segmentation control named as the compensating control | G4 | add FR5 zones/conduits to the Part 9 story + a segmented-plant variant | P2 |
| CN2 | MQTT DCMD→coil may not be demonstrated end-to-end | G9 | verify on twin or label | P2 |
| B16 | no 62443/NIST control lens anywhere (this card is the first) | G8 | Stage 5 adds the column | P1 |
