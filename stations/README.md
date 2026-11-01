# Stations — the TBD controller as a real OT network

Everywhere else in this lab you drive **one** controller (the twin, or a real board). This
folder runs it the way a plant really looks: **separate stations on a network**, each a
container, speaking **real protocols you can capture**. It's the multi-node model from the
Week-4 wind-turbine lab, applied to the TBD controller — and it stands alone (no wind farm).

```
            plant network  10.20.30.0/24
  ┌───────────────┐   Modbus/TCP :502   ┌───────────────┐
  │   hmi  .20    │ ───── reads ──────▶ │    plc  .10   │  the controller:
  │ SCADA :8800   │                     │ twin + bridge │  QEMU StampLC twin
  └───────────────┘                     │  :502 Modbus  │  (real firmware)
  ┌───────────────┐   read + WRITE      │  :80  HTTP    │
  │ engineer      │ ─── (CWE-306) ────▶ │  :8088 Studio │
  │ Modbus master │                     └───────────────┘
  └───────────────┘            ▲
  ┌───────────────┐            │ passive span
  │   capture     │ ───────────┘  Wireshark/tshark on :502 / :20000
  └───────────────┘
```

| Station | What it is | Ports |
|---|---|---|
| **plc** (10.20.30.10) | the controller — the QEMU twin + bridge; its own Modbus/TCP and HTTP are bound on the plant network | 8088 Studio, 8080 web HMI, 502 Modbus, 80 HTTP |
| **hmi** (10.20.30.20) | a SCADA HMI that **polls the controller over Modbus** and shows a live dashboard | 8800 |
| **engineer** (plant, dynamic IP) | an engineering / attacker workstation — a **Modbus master** that reads and (on the vulnerable build) writes the controller with no auth | — |
| **mqtt** (10.20.30.40) | the phone broker + sensor page: phones scan a QR code and publish their sensors; the controller subscribes | 8090 phone page, 1883 MQTT |
| **capture** | a passive sniffer (Wireshark/tshark) that sees the Modbus **and MQTT** frames between the stations | writes `captures/plant.pcap` |

## Run it

### In a GitHub Codespace (recommended)
The devcontainer already includes Docker-in-Docker, so nothing to install.

**First, stop your own single-twin setup from the earlier parts.** This stack publishes host
ports **8088** (plc Studio), **8090** + **1883** (mqtt phone broker) and **8800** (hmi). If your
own Part 1 twin/Studio (`./qemu/studio.sh`) still holds **8088**, or the phone bridge
(`mqtt/share.sh`) still holds **8090/1883**, `docker compose up` fails with a port-in-use (bind)
error. Stop them first:

```bash
./qemu/twin-status.sh stop                        # stop your own twin + Studio (frees :8088)
pkill -f "node server.js" 2>/dev/null || true     # stop the phone bridge if you ran mqtt/share.sh (frees :8090/:1883)
```

```bash
./qemu/fetch-qemu.sh                              # once: fetch the twin (mounted into the plc station)
docker compose -f stations/docker-compose.yml up --build
```

Open the forwarded **8800** (the HMI dashboard) and **8088** (the Studio) from the **Ports**
tab. The HMI tiles update every second — every value arrived as a real Modbus frame on the
wire.

### Drive the controller from the engineer station (network plane)
```bash
# read the whole process image over Modbus
docker compose -f stations/docker-compose.yml logs -f engineer

# command the controller over the network — accepted with NO authentication on the
# vulnerable build (CWE-306); refused on the secure build unless an operator is in REMOTE:
docker compose -f stations/docker-compose.yml run --rm engineer python3 /app/engineer.py write-coil relay2 1
docker compose -f stations/docker-compose.yml run --rm engineer python3 /app/engineer.py write-setpoint 321
```

### See the real frames (the span port)
```bash
docker compose -f stations/docker-compose.yml logs -f capture        # live decoded Modbus + MQTT
# or open the pcap in Wireshark:
#   stations/captures/plant.pcap   (filters: modbus · mqtt · mqtt.topic contains "phone/")
```

### Phones as network inputs (MQTT)
Make port **8090** public in the Ports tab, open it, and scan the QR code with your phone. Every
sensor you share is published as `phone/<id>/<sensor>` (plain JSON) to the broker; the controller
station subscribes (its entrypoint runs `mqtt connect 10.20.30.40`) and maps them to **network**
points — separate from the IN1–IN8 terminals, so a wired switch and a phone never share an input.
In the capture you can read each value in the clear (`mqtt.topic`, `mqtt.msg`): anyone on this
network can read it, and anyone who can publish can forge it. The phone page's **spoof** and
**transmit off** controls are those two attacks (integrity and availability).

## The point
This is the **network plane**. The same points the ladder drives over the **I²C bus** (the
bus plane, inside the controller) are here read and written over **Modbus/TCP between
machines**, and phone sensors arrive as **MQTT** publishes. Two paths to one process — and on
the vulnerable build the network path has no authentication in either protocol. Run `./qemu/build-twin.sh secure` and set `PROFILE=secure` in the compose
to watch the engineer's writes get refused.

## Secure / malicious profiles
Set `PROFILE` in `docker-compose.yml` (or `PROFILE=secure docker compose … up`):
`vuln` (default) · `secure` (needs `./qemu/build-twin.sh secure` first) · `malicious`.

## Notes
- The **plc** station mounts the repo at `/work` and runs the same `./qemu/studio.sh` you use
  directly — just with `W5_BIND=0.0.0.0` so its Modbus/HTTP are reachable on the plant network.
- The **capture** station runs in the host network namespace (like the wind-turbine Wireshark
  sidecar) so it sees unicast traffic between the other containers on the Docker bridge.
- Give the Codespace a little room: the twin plus four containers is comfortable on a 4-core / 8 GB machine.
