# Part 9 — Remediation: the secure build

Parts 6–8 (and the firmware console throughout) demonstrated how this controller gets
owned: a hardcoded login, an unsigned OTA, a dormant supply-chain payload, an
unauthenticated clock. This part closes the loop — **how each weakness is fixed**, and
how you *prove* the fix without trusting a changelog.

The whole firmware flips between the teaching image and a hardened one with **one
compile switch**. Every intentional weakness is wrapped:

```c
#ifdef ENABLE_CWE_DEMOS
    ... the vulnerable teaching code ...     // the default build
#else
    ... the hardened counterpart ...         // the secure build
#endif
```

- **Teaching build** (`pio run -e m5stamplc`) — `ENABLE_CWE_DEMOS` on. The weaknesses are
  present; this is the image Parts 6–8 attack.
- **Secure build** (`pio run -e m5stamplc_secure`) — the env drops `-DENABLE_CWE_DEMOS`
  and defines `-DSECURE_BUILD`, so every weakness compiles out and its fix compiles in.

```bash
cd sbom/firmware
pio run -e m5stamplc            # teaching image (weaknesses present)
pio run -e m5stamplc_secure     # hardened image (weaknesses fixed)
```

The secure image announces itself at boot (`✓ SECURE BUILD — intentional weaknesses
compiled out`) instead of the teaching image's CWE list.

---

## What was fixed, where, and how to prove it

Each row is a weakness you met earlier, the hardened behaviour in the `#else`, and the
observation that distinguishes the two builds. File paths are under `sbom/firmware/src/`.

### Credentials & sessions — `plc_auth.cpp`

| CWE | Teaching weakness | Secure build | Prove it |
|---|---|---|---|
| 798 / 259 / 1392 | `admin:openplc`, a `service` password derived from the device MAC, and `operator:1234` all log in | no hardcoded, default, or MAC-derived account — only a **provisioned** operator password | `login admin openplc` is rejected on the secure build, accepted on the teaching build |
| 256 / 257 | user password stored and shown in plaintext (recoverable) | stored only as a **salted SHA-256** hash; never recoverable | `svcinfo` / the user-pw readout shows `(hashed - not recoverable)` |
| 521 | policy *accepts* weak (≤8, A–Z0–9) and *refuses* strong | requires **≥12 chars and ≥3 character classes** | try to set a 20-char mixed password — refused on teaching, accepted on secure |
| 307 | no lockout — brute-forceable | **30 s lockout after 5** failed logins | 6 bad logins → the 6th is refused even with the right password on the secure build |
| 330 | session token = `millis() ^ constant` (predictable) | **128-bit token from the hardware CSPRNG** (`esp_random`) | two tokens on the teaching build are correlated in time; on secure they are not |

### Update path — `eth_ota.cpp`

| CWE | Teaching weakness | Secure build | Prove it |
|---|---|---|---|
| 494 | `verify_firmware()` is a no-op (`return true`) — any binary boots | the image must carry a valid **HMAC-SHA256 signature** (`X-Firmware-HMAC`) over the whole upload, keyed by a provisioning secret; a bad/absent signature is **aborted, never flashed** | POST an image with no/der wrong `X-Firmware-HMAC` → `403`, no reboot (Part 6's push boots on teaching, is refused on secure) |
| 306 | no authentication on the upload endpoint | upload requires **HTTP Basic auth**; `401` otherwise | POST without `Authorization` → `401` on secure |

### Protocol writes — `modbus_tcp_server.cpp`

| CWE | Teaching weakness | Secure build | Prove it |
|---|---|---|---|
| 306 | any peer can write coils/registers on `:502` — including the e-stop coil — with no check | network writes (FC 5/6/15/16) are **refused unless the controller is in the supervised `REMOTE` position** (the key-switch control); the write is dropped and audited | an unauthenticated FC5 write in RUN mode changes a coil on teaching; on secure the audit log shows `REFUSED ... not in REMOTE mode (CWE-306)` and the coil is unchanged |

### Services & input handling — `debug_telnet.cpp`, `eth_http.cpp`

| CWE | Teaching weakness | Secure build | Prove it |
|---|---|---|---|
| 489 | unauthenticated debug console on telnet `:23` | the telnet service is **never started** | connect to `:23` → refused on secure, open on teaching |
| 78 | `ping <target>` passes unsanitized input to `system()` | ping **validates the target is a bare IPv4** before use (and the only caller, telnet, is off) | `ping 8.8.8.8; reboot` injects on teaching; rejected on secure |
| 22 | log viewer builds a path straight from `?file=` (`../` escapes the dir) | path **rejects separators and `..`** — a bare filename only | `/logs?file=../../etc/x` reads out on teaching, `rejected` on secure |
| 200 / 1273 | every HTTP response leaks `X-Device-MAC` (the value the service password derives from) | the MAC header is **not emitted** | `curl -i` the HMI — `X-Device-MAC` present on teaching, gone on secure |

### Wall-clock — `time_sync.cpp`

| CWE | Teaching weakness | Secure build | Prove it |
|---|---|---|---|
| 197 | the SNTP time is stored in a **signed 32-bit** field; a value past 2038-01-19 wraps to 1901 | the epoch is stored as **64-bit** end-to-end; no wrap | arm the rogue NTP (Part 8) and `ntpsync` → teaching `clk` reads **1901 WRAPPED**, secure `clk` reads the real **2038** date, Y2038-safe |

### Memory safety — `plc_core.cpp`, `eth_http.cpp`

| CWE | Teaching weakness | Secure build | Prove it |
|---|---|---|---|
| 787 / 120 | an unauthenticated `/api/tag` write copies attacker-controlled bytes into a fixed scratch buffer with **no bound**; that buffer sits immediately before the I/O force image, so an overlong write **corrupts the force masks and energizes a relay** — bypassing the audited force API, so the force has **no source in the log** | `plc_tag_write()` **bounds-checks** the copy into a standalone buffer; the force image has no adjacent attacker-writable buffer, so the overflow cannot occur | the derive tool emits a 48-byte payload; on the teaching build `GET /api/tag?data=…` drives `relay0` ON with **no `FORCE` audit entry**, on the secure build the same request does nothing |

**The overflow, and the "memory map" lesson.** The scratch buffer and the force masks live
in one struct (`ForceImage`), so the overflow offset is a fact of *that build*.
`tools/derive-force-offset.py` reads the ELF's symbols, confirms the layout still fits, and
prints the exact bytes and URL:

```bash
pio run -e m5stamplc
python3 tools/derive-force-offset.py            # -> PAYLOAD_HEX=… and the /api/tag URL
```

Change the struct and you must re-derive — the tool and the CI smoke-leg (`overflow.*`) both
fail loudly if the layout moves, instead of a silently-dead exploit. Run the same tool
against the **secure** ELF and it reports *"no overflow target"*: that build has no such
struct, so there is nothing to corrupt. This is the capstone of **build-break-and-secure** —
the attack, the fix, and the tooling that keeps both honest across rebuilds, in one switch.

---

## What the secure build does *not* fix (and why)

Two weaknesses the course names are **transport / trust** problems a compile switch can't
close on the twin — call them out in your finding rather than pretending they're gone:

- **CWE-319 cleartext (Modbus `:502`, HTTP)** — the real control is TLS / an encrypted
  transport (or a segmented OT network), not a firmware flag. Out of scope for the twin.
- **CWE-345 unauthenticated time source** — the 64-bit store (CWE-197) stops the *wrap*,
  but the device still *trusts whoever answers* the time query. The full control is
  authenticated time (NTS) or a disciplined local clock; the 64-bit fix only removes the
  catastrophic truncation, not the AiTM itself.

This is the Part-6/7 lesson restated: some fixes are a line of code, and some are an
architecture (signing, TLS, network segmentation, secure boot).

---

## Verify it yourself — the honest way

A changelog that says "fixed" is not evidence. You have the two images; establish the
difference the way you established the attacks.

```bash
# build both
cd sbom/firmware && pio run -e m5stamplc && pio run -e m5stamplc_secure

# boot each in the twin and run the same probe (example: the NTP wrap from Part 8)
#   teaching -> clk WRAPS to 1901 ;  secure -> clk stays in 2038
```

The repo's CI (`.github/workflows/twin-smoke.yml`) boots the **teaching** image and
asserts all 14 invariants still hold (the weaknesses are deliberately *present* there).
The secure image is validated separately: it boots, announces the hardened banner, and
the Part-6/7/8 attacks are refused (NTP no-wrap, Modbus write refused, telnet closed).

> **Do (your Part 9 finding):** pick **one** CWE above. Build both images, trigger the
> attack from its earlier Part against each, and record the *observation* that
> distinguishes them (e.g. "`clk` read `1901` on `-e m5stamplc`, `2038` on
> `-e m5stamplc_secure`, same armed NTP"). Then one sentence: is this fix a *line of code*
> or an *architecture*, and what would a reviewer need to trust that the shipped image is
> actually the secure one? (Back to Part 7: a reproducible build or a signed attestation —
> not a glance at the source.)
