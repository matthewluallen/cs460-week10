# Chain card — Trust chain: phone/sensor claim → logic (false-data injection)

> Stage 3, chain #5. Merges into the Signal Journey at **link 3** (logic — a word-input the rung reads).
> Part 8 + extension E3 (the graded FDI assignment). Firmware: `mqtt_sensors.cpp`.

- **Consequence:** a value the controller trusts (a phone sensor) is withheld (availability) or falsified
  (integrity), changing what the logic decides.
- **Status:** draft.

## Links
| # | From → To | Lens | Establishes | Does NOT establish | Technique | CWE | Obs? |
|---|---|---|---|---|---|---|---|
| 1 | phone publishes `phone/<id>/<sensor>` (no broker auth) | Wire · MQTT | a client can publish a sensor value | that the value is real, or the id genuine — **an id is a claim, not a login** | T1692.002 Reporting Message | CWE-306 | yes (tshark) |
| 2 | bridge → firmware subscribes → word input (e.g. IW3 = `tilt_b` reg 146) | Code · `mqtt_sensors.cpp` | the value lands in the **input image** the rung reads | authenticity / freshness by itself | **T0835** | CWE-345 | yes (`mqtt status`) |
| 3 | rung compares (β>30 set / <25 clear) → RELAY 3 | Logic | the controller **acts** on the claimed value | that the claim was true | T1692.001 | — | yes (relay) |
| 4a | **withhold** (Transmit OFF) → point stale after 8 s, holds last value | Code | availability loss → stale handling | — | T0815 Denial of View (availability) | CWE-672 (stale use) | yes |
| 4b | **spoof** → published value leaves the real reading | Wire→Logic | a fresh, from-known-id, **false** value drives the relay | — | T1692.002 | CWE-345 | yes (trend graph) |

## Bowtie — barriers (the E3 plausibility gate is the taught defense)
- **Message authentication / per-id pinning** (`mqtt pin`; D3-MAN) — reduces id spoofing; not full auth.
- **Freshness bit** (stale after 8 s) — catches withholding; the *fix* is to require fresh.
- **Range + rate plausibility gate** (E3: require fresh ∧ in-range ∧ bounded rate) — the compensating
  control the assignment makes students build and prove the real reading still passes.
- **Out-of-band cross-check** (a second independent sensor) — the control the lab asks "name" but doesn't
  implement.
- *(If an ML anomaly detector is ever added on the stream → **ATLAS AML.T0043** Craft Adversarial Data.)*

## Residual risk
"Even with the gate, the alarm still trusts ___" — a single phone id and the plausibility bounds; a
patient attacker who stays in-range + fresh still passes. The gate is **detect/limit, not prevent**.

## Gaps
| # | Gap | Type | Fix | Pri |
|---|---|---|---|---|
| S1 | The E3 plausibility gate is **graded prose**, no machine-checkable anchor. | G5/G6 | a verifier that submits a spoof + a real reading and checks refuse/pass. | P1 |
| S2 | Freshness is **phone-timed** on the trend graph but **twin-timed** on the controller (the twin clock runs ~0.45× real), so the 8 s stale mark differs — already noted in the phone page; ensure the assignment says so. | G7 | doc note | P3 |
| S3 | "Name an out-of-band cross-check" is asked but no artifact shows one — the strongest compensating control is conceptual only. | G4 | optional: a second sensor / cross-check demo. | P2 |
