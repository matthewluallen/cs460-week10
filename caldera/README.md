# E5 — CALDERA: emulating the phone-sensor attack as a full kill chain

> Extension **E5** (builds on Part 8 / E3; pairs with **E4**, the detector it tests). The red side
> of the red/blue loop: E3 is the attack by hand, E4 puts a model in the way, E5 automates the
> whole thing — including how the attacker got in. Student assignment:
> [`docs/assignment-caldera-e5.html`](../docs/assignment-caldera-e5.html).

The Threat Map leaves the **CALDERA** cell empty for most lab attacks for an honest reason:
MITRE's OT plugin [`caldera-ot`](https://github.com/mitre/caldera-ot) ships six protocol plugins
(modbus, bacnet, dnp3, profinet, iec61850, gems) and **none for MQTT**. So the phone-sensor
false-data injection (Part 8 / E3) has no off-the-shelf plugin — it needs a **custom ability**.
This folder is that ability, plus the C2 station and the delivery step that puts an agent in place
to run it. It turns the single `mosquitto_pub` line on the Threat Map into an automated,
event-logged operation a red team would actually run.

Everything here is a teaching artifact; nothing exploits anything. All ATT&CK-for-ICS technique
IDs match the Threat Map.

## The chain this models

```
 (earlier in the real attack)        CALDERA C2 (10.20.30.50)
  USB dropper / phishing attach.     ┌───────────────────────────┐
  T0847 / T0865 · CAPEC-452/163      │  adversary profile +       │
        │                            │  custom MQTT abilities     │
        ▼  deploy-sandcat.sh         └─────────────┬─────────────┘
  engineer station (10.20.30.30) ◀── agent beacons ─┘
        │  Sandcat agent runs the operation:
        │     1. T0846  Discover the MQTT broker
        │     2. T1692.002  Spoof a phone sensor reading   ─┐  forged
        │     3. T1692.001  Spoof a phone button (command) ─┤  phone/<id>/… →
        ▼                                               ▼
  mqtt broker (10.20.30.40) ──────────────────▶ the StampLC controller (10.20.30.10)
                                                 ingests them as network points (T0835
                                                 Manipulate I/O Image) — Part 8 / E3.
```

**The delivery step is the point of `deploy-sandcat.sh`.** An agent does not appear on an
engineering workstation by itself; it arrives through an earlier link in the chain — a USB stick
(ATT&CK ICS **T0847 Replication Through Removable Media**, **CAPEC-452 Infected Hardware**) or a
phishing attachment (**T0865 Spearphishing Attachment**, **CAPEC-163 Spear Phishing**). The script
stands in for what that dropped/opened payload would run: fetch the agent from the C2 and execute
it (**T0863 User Execution**, **T1105 Ingress Tool Transfer**). Making initial access explicit and
mapped is half the lesson — everything after it only works because a human was tricked first.

## Run it (in the stations stack)

```bash
# 1. bring up the plant incl. the CALDERA C2 (first build is slow: it builds CALDERA + the UI)
docker compose -f stations/docker-compose.yml up --build        # or: … up --build caldera mqtt plc

# 2. land the agent on the engineer station (the emulated USB/phishing delivery)
docker compose -f stations/docker-compose.yml run -d --rm --name cs460-dropper engineer /app/deploy-sandcat.sh

# 3. run the operation and print the event log (discover → spoof → command)
docker compose -f stations/docker-compose.yml exec caldera python3 /usr/src/app/run-scenario.py
```

Then watch the consequence the way you did in Part 8: the controller's `mqtt status`, the HMI tile,
the Signal Journey — and, if you built it, the **E4 anomaly detector** flags (or, for a crafted
spoof, misses) the injected value. CALDERA is the red-team automation; E4 is the detection it tests.

Prefer the UI? Open **http://localhost:8888** (login `red` / `admin`), find the agent, and run the
**CS460 Phone-Sensor False-Data Injection** adversary against the **CS460 Phone-Sensor Facts**
source. The REST script and the UI do the same thing; the script is what CI / grading uses.

## What's here

| File | What it is |
|---|---|
| `plugins/mqtt_phone/data/abilities/discovery/…`  | **T0846** Discover the MQTT broker |
| `plugins/mqtt_phone/data/abilities/impair-process-control/…` | **T1692.002** Spoof sensor reading · **T1692.001** Spoof button (command) (formerly T0856/T0855) |
| `plugins/mqtt_phone/data/adversaries/…` | the ordered profile: discover → spoof → command |
| `plugins/mqtt_phone/data/sources/…` | the facts (broker, phone id, sensor, value) you edit to retarget |
| `plugins/mqtt_phone/data/payloads/mqtt_spoof.py` | no-deps raw-socket MQTT publisher the abilities run |

The C2 station itself (Dockerfile, entrypoint, `run-scenario.py`) is in
[`stations/caldera/`](../stations/caldera/); the delivery script is
[`stations/engineer/deploy-sandcat.sh`](../stations/engineer/deploy-sandcat.sh). The abilities are
bind-mounted into the C2 and merged into stockpile at startup, so you can edit them here without
rebuilding the image.

## Retargeting

Edit the fact source (no rebuild): point `mqtt.phone.topic` / `mqtt.phone.key` / `mqtt.phone.value`
at any analog point (`tilt_b` → `beta`, `sound` → `level`, …), or `mqtt.phone.button` at `btn1`/`btn2`.
The payload builds the JSON itself from key/value, so values pass cleanly through the C2 (passing raw
JSON as a fact gets shell-escaped — the button ability keeps its literal payload in the ability YAML).

## Honest caveats

- This is a **custom teaching ability**, not a vendor-maintained plugin. It's a faithful emulation of
  the attacker's actions, not a certified tool.
- The agent must be able to reach the broker on the plant network, and the delivery is *emulated* —
  no real USB/phishing exploitation happens.
- CALDERA emulates the **attacker**. The consequence on the controller and its **detection** are the
  twin, the analyzer, and extension E4 — not this folder.
- Pinned to CALDERA v5.3.0. The linux Sandcat agent is compiled on demand by the C2.
