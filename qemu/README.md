# QEMU twin — run the firmware with no hardware

Runs the *real*, de‑branded StampLC firmware — the same `plc>` console and the same
vulnerable Modbus / HMI / OTA services you'd meet on the bench — entirely in software.
**Parts 6 and 7 use this.** (It's also fair game as enrichment for the earlier parts.)

```bash
./qemu/fetch-qemu.sh        # one-time: downloads the prebuilt twin for your platform
./qemu/run.sh               # boots the firmware (leave it running)
# in another shell:
./qemu/console.sh           # the plc> console   (quit: Ctrl-C)
```

## Helper scripts (short to type, annotated inside)

Every long or fiddly command in Parts 6–7 has a small wrapper. You never have to
retype the full command — but you can still *see* it: open the script to read each
part annotated, and when you run it, it prints the exact command before executing.

| Run this | Instead of typing | Used in |
|---|---|---|
| `./qemu/run.sh` | the full `qemu-system-xtensa …` line (knobs: `ICOUNT`, `FLASH_IMG`, `W5_BIND`, `ALLOW_REBOOT`) | 6, 7 |
| `./qemu/console.sh` | `nc -U /tmp/plc-serial` (+ the quit incantation) | 6, 7 |
| `./qemu/ota-push.sh [file]` | `curl -s -X POST --data-binary @file http://127.0.0.1:80/api/upload` | 6 |
| `./qemu/relay.sh [name]` | `curl -s …:8080/api/points \| grep -o '"n":"relay0"[^}]*'` | 6, 7 |
| `./firmware-ota/show-bug.sh` | `grep -rn 'verify_firmware\|verified\|Update.begin' sbom/firmware/src/` | 6 |
| `./supply-chain/diff-images.sh` | `ls -l` the two images + compute the byte delta | 7 |
| `./supply-chain/run-malicious.sh` | `ICOUNT=off FLASH_IMG=…MALICIOUS.bin ./qemu/run.sh` | 7 |
| `./supply-chain/arm.sh` / `benign.sh` | `echo ARM > /tmp/c2_state` / `rm -f /tmp/c2_state` | 7 |

## Why the twin needs one capability (and not root)

The OTA service listens on **port 80** inside the firmware, and the emulated W5500
bridge binds that same port on the host. Port 80 is *privileged* (< 1024), and a
Codespace runs as a normal user — so the bind would be refused. We don't lower the
privileged-port boundary (Codespaces makes that sysctl read-only) and we **don't run
the emulator as root**. Instead `fetch-qemu.sh` grants the qemu binary exactly one
Linux capability:

```bash
sudo setcap cap_net_bind_service=+ep  qemu/twin/qemu-twin-linux-*/qemu-system-xtensa
```

That's the whole fix — the single capability "may bind low ports," nothing else.
`run.sh` re-applies it automatically if it's ever missing. (This is itself a nice
least-privilege example for the course: the narrowest grant that makes the thing work.)

## Under the hood

The prebuilt binary is a **patched** Espressif QEMU (a stock `qemu-system-xtensa`
can't model the StampLC's I²C sensors, LCD, or W5500, so the firmware stalls at
startup). Prebuilt bundles + the firmware images are hosted on the repo's
`qemu-twin-*` release. For the full build‑from‑source recipe, the live interfaces
(`plc>`, Modbus/TCP, HTTP‑HMI, QMP I/O injection), dated provenance, and the
gotchas, read **[../docs/QEMU-Twin.md](../docs/QEMU-Twin.md)**.
