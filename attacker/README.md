# The attacker host (optional, realistic setup)

By default the lab's rogue servers run on `127.0.0.1` — simple, but the "attacker"
then shares the device's own loopback. For a more realistic picture, run them in a
**separate container at a distinct IP** (`172.30.0.10`), so the StamPLC twin sees the
attacker as its own host on the network.

This needs Docker. In a **Codespace** it's already enabled (the dev container ships
the `docker-in-docker` feature); locally you just need Docker.

## Start it

```bash
./attacker/up.sh        # docker compose up -d --build, wrapped as one short command
```

That brings up `lab-attacker` at **172.30.0.10**, running:
- the rogue **NTP** time server on **:1123** (udp+tcp) — Part 8,
- the rogue **C2 / update** server on **:9000** (tcp) — Part 7 (also published to the
  host's `127.0.0.1:9000` for the Part-7 malicious image, whose beacon target is fixed).

## Point the twin at it

Part 8 (NTP AiTM) — the device targets the attacker's IP instead of localhost:

```
plc> ntpsync 172.30.0.10 1123          # or: netcfg ntp 172.30.0.10 1123 ; netcfg save
plc> clk                               # a benign, current date
```

## Arm / reset (play the attacker)

```bash
./attacker/arm.sh        # NTP serves a post-2038 time; C2 serves the kill-switch trigger
./attacker/benign.sh     # back to normal
```

Then on the device:

```
plc> ntpsync 172.30.0.10 1123          # armed -> the 32-bit clock wraps
plc> clk                               # 1902-04-27 — WRAPPED (same post-2038 step as Part 8)
```

## How it reaches the device

The twin's emulated W5500 bridges the guest's outbound connections onto the host, so
the guest can reach any address the **host (the Codespace / dev container) can route
to** — including this container's `172.30.0.10` on the `labnet` bridge. Nothing special
is needed on the firmware: `ntpsync`/`netcfg ntp` already take any IP.

## Stop it

```bash
./attacker/down.sh      # docker compose down, wrapped as one short command
```
