# QEMU twin — run the firmware with no hardware

The quick start below runs the committed classroom image. Both `m5stamplc` and
`m5stamplc_secure` are required; `ENABLE_CWE_DEMOS` is the teaching switch. See
[the QEMU twin guide](../docs/QEMU-Twin.md) for the exact-artifact physics and replay.

## Historical classroom quick start

Runs the *real*, de‑branded StampLC firmware — the same `plc>` console and the same
vulnerable Modbus / HMI / OTA services you'd meet on the bench — entirely in software.
**Parts 6 and 7 use this.** (It's also fair game as enrichment for the earlier parts.)

```bash
./qemu/fetch-qemu.sh        # one-time: downloads the prebuilt twin for your platform
./qemu/studio.sh            # boots the twin, serves the Studio on :8080, muxes the console
# in another shell (optional — only if you want the text console too):
./qemu/console.sh           # the plc> console   (quit: Ctrl-C)
```

`studio.sh` starts everything you need: the twin, the browser Studio (**Connect twin**),
and a multiplexed console. `console.sh` attaches to that same console, so the text console
and the browser Studio run **at the same time**. (Earlier, a second client on the twin's
single-client serial socket would connect but get nothing — the "stuck console". The bridge
now owns the one serial connection and fans it out, so that can't happen.) You only need
`./qemu/run.sh` directly to boot a *different* profile (`secure`, `malicious`) by itself.

**The Studio server only obeys its own page.** `studio.sh` serves the Studio and bridges it to the
twin, so it can move the (emulated) process. It listens on localhost (Codespaces forwards that), a
browser request must come from the Studio's own page (checked by Origin on the console WebSocket and
on every write), Modbus writes need POST (so a link or image on another site can't flip a coil), and
other sites can't read values through it. Circuit views set the twin's inputs **at the chip** through
`/field` — exactly two expander properties, never raw QMP. `python3 qemu/test-bridge-security.py`
checks all of this (CI runs it). A real attacker on the *plant network* is a different story — the
twin's own Modbus port is unauthenticated on the vulnerable build on purpose (Parts 1 and 6).

**Already running? Nothing is booted twice.** `studio.sh`, `run.sh` and
`assessment/verify-lab.py --boot-twin` all check first. A twin counts as running when its
`qemu-system-xtensa` process is alive on the console socket **and** QEMU's control (QMP) socket
reports it running — a socket file left by a crashed twin does not count, and is cleared on the
next boot. If the running twin is the build you asked for (or you did not name one) it is reused;
if you named a different build you are told how to switch:

```bash
./qemu/twin-status.sh            # RUNNING / STARTING / ORPHANED / STOPPED, and which build
RESTART=1 ./qemu/run.sh secure   # replace the running twin (an open Studio tab reconnects)
./qemu/twin-status.sh stop       # stop the twin and its Studio server
./qemu/test-twin-status.sh       # regression test for all of the above (also runs in CI)
```

> **On an Apple-Silicon Mac?** There's no native-macOS twin — the patched QEMU is built
> for Linux only (the release ships `qemu-twin-linux-x86_64` and `-linux-arm64`; stock
> Homebrew `qemu-system-xtensa` can't model the StampLC's I²C / LCD / W5500, so it stalls
> at boot). Run the twin in a **Codespace**, or open this repo in a **local VS Code Dev
> Container** (*Dev Containers: Reopen in Container*) — that's Linux arm64, so
> `fetch-qemu.sh` pulls the matching `qemu-twin-linux-arm64` build and everything below
> works unchanged.

## Helper scripts (short to type, annotated inside)

Every long or fiddly command in Parts 6–7 has a small wrapper. You never have to
retype the full command — but you can still *see* it: open the script to read each
part annotated, and when you run it, it prints the exact command before executing.

| Run this | Instead of typing | Used in |
|---|---|---|
| `./qemu/run.sh` | the full `qemu-system-xtensa …` line (knobs: `ICOUNT`, `FLASH_IMG`, `W5_BIND`, `ALLOW_REBOOT`, `RESTART`); reuses a twin that is already running | 6, 7 |
| `./qemu/twin-status.sh [stop]` | `pgrep -f qemu-system-xtensa` + a QMP `query-status`, and the image on its command line | all |
| `./qemu/console.sh` | attaches the `plc>` console via the bridge (`nc 127.0.0.1 8081`), so it coexists with the Studio; falls back to `nc -U /tmp/plc-serial` if no bridge is up | 6, 7 |
| `./qemu/ota-push.sh [file]` | `curl -s -X POST --data-binary @file http://127.0.0.1:80/api/upload` | 6 |
| `./qemu/relay.sh [name]` | `curl -s …:8080/api/points \| grep -o '"n":"relay0"[^}]*'` | 6, 7 |
| `./firmware-ota/show-bug.sh` | `grep -rn 'verify_firmware\|verified\|Update.begin' sbom/firmware/src/` | 6 |
| `./supply-chain/diff-images.sh` | `ls -l` the two images + compute the byte delta | 7 |
| `./supply-chain/run-malicious.sh` | `RESTART=1 ICOUNT=off FLASH_IMG=…MALICIOUS.bin ./qemu/run.sh` | 7 |
| `./supply-chain/arm.sh` / `benign.sh` | `echo ARM > /tmp/c2_state` / `rm -f /tmp/c2_state` | 7 |
| `./bus-analysis/tap.sh [secs]` | `curl -X POST localhost:8080/bus/capture?seconds=6` — records YOUR running twin's I²C bus (→ `.sr`/`.pvs`/`.vcd` in `bus-analysis/out/`, sigrok decode of 0x59) | bus |
| `./sbom/scan-running.sh` | fingerprint the image the twin booted → SPDX + CycloneDX → grype → RISK | SBOM |
| `python3 qemu/fw_identity.py <image>` | read partition table, boot slot, ESP-IDF version and app ELF SHA-256 from a flash image | SBOM |

### Studio server endpoints (all same-origin; writes need POST)

| Endpoint | What it does |
|---|---|
| `GET /field`, `POST /field?bit=N&v=0\|1` | the AW9523 expander's `di`/`relays` bytes through QMP (chip level) |
| `POST /bus/capture?seconds=N` (alias `POST /bustap?secs=N`), N ≤ 10 | live I²C capture of the running twin: QMP `logfile` + `trace-event-set-state esp32_i2c_observe` on, N s, off; converted to `.vcd`/`.sr`/`.pvs`/`.json` + sigrok decode. The trace event is never left on (also switched off at server start). |
| `GET /bus/captures`, `GET /bus/files/<name>` | list / download captures (strict file-name pattern) |
| `GET /twin/identity[?hash=1]` | the running twin's image: profile, boot slot, ESP-IDF, app ELF SHA-256 (from the flash image, not the firmware) |
| `GET /fw/api/log`, `GET /fw/api/points` | read-only proxy to the firmware's HTTP HMI. If this server holds the firmware's own port (8080), it answers 503 `shadowed` and Provenance Studio reads `plc> log` over the console instead; `BRIDGE_PORT=8088 ./qemu/studio.sh` frees 8080 for the firmware. |

## Why the twin needs one capability (and not root)

The OTA service listens on **port 80** inside the firmware, and the emulated W5500
bridge binds that same port on the host. Port 80 is *privileged* (< 1024), and a
Codespace runs as a normal user — so the bind would be refused. We don't lower the
privileged-port boundary (Codespaces makes that sysctl read-only) and we **don't run
the emulator as root**. Instead `fetch-qemu.sh` grants the qemu binary exactly one
Linux capability (shown so you can read it — **you never type this**, the scripts run it):

```bash
sudo setcap cap_net_bind_service=+ep  qemu/twin/qemu-twin-linux-*/qemu-system-xtensa
```

That's the whole fix — the single capability "may bind low ports," nothing else.
`fetch-qemu.sh` applies it on setup and `run.sh` re-applies it automatically if it's ever missing. (This is itself a nice
least-privilege example for the course: the narrowest grant that makes the thing work.)

## Under the hood

The prebuilt binary is a **patched** Espressif QEMU (a stock `qemu-system-xtensa`
can't model the StampLC's I²C sensors, LCD, or W5500, so the firmware stalls at
startup). Prebuilt bundles + the firmware images are hosted on the repo's
`qemu-twin-*` release. The referenced custom patch source is not in the inspected
release or repository branches, so a reproducible model build remains a provenance
gap. The current guide records observed binary interfaces, ownership/deadline
qualification and the limits of the source/model evidence:
**[../docs/QEMU-Twin.md](../docs/QEMU-Twin.md)**.
