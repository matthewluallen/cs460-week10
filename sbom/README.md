# Part 2 — Software Bill of Materials (SBOM)

An **SBOM** lists what's *inside the software*: the libraries, packages and versions
a build pulls in. Security teams use it to answer "do we ship anything with a known
CVE?" You'll generate SBOMs in two standard formats, compare them, scan one for
vulnerabilities, and — the honest part — see where an SBOM's view stops.

Tools (installed for you): **syft** (generates SBOMs) and **grype** (scans an SBOM
for CVEs).

---

## Exercise 1 — the device's real dependencies

Open [`firmware/platformio.ini`](firmware/platformio.ini). That's the actual build
manifest for the StampLC firmware — and you can **compile it yourself** in Codespaces
or local VS Code (see [`firmware/BUILD.md`](firmware/BUILD.md)). Its `lib_deps` are the
real components compiled into the device:

| Component | Version | What it is |
|---|---|---|
| `m5stack/M5StamPLC` | 1.2.0 | board support (relays, DI, RTC, sensors) |
| `m5stack/M5Unified`, `M5GFX` | 0.2.21 / 0.2.28 | display + device HAL |
| `bblanchon/ArduinoJson` | 6.21.6 | JSON parsing (web API, config) |
| `knolleary/PubSubClient` | 2.8 | **MQTT** client |
| `adafruit/Adafruit BME680` | 2.0.6 | environmental sensor driver |
| `khoih-prog/Ethernet_Generic` | 2.8.1 | W5500 PoE Ethernet |

Underneath those sits the **Arduino-ESP32 / ESP-IDF framework**, which bundles
`mbedTLS`, `lwIP`, `wpa_supplicant` and FreeRTOS — the TCP/IP and crypto stacks where
most *embedded* CVEs actually live.

**Do:** pick **two** of these components, find the version, and look each up in the
[NVD](https://nvd.nist.gov/). Record whether that pinned version has a known CVE, and —
if so — whether a fix would be a firmware patch or a library bump. Note your uncertainty
(a listed dependency is not proof that code path is reachable).

---

## Exercise 2 — SBOM of the firmware your twin is running

With your twin running (`./qemu/studio.sh`), first note its fingerprint at the console:
`plc> diag json` → `elf_sha256`. Then:

```bash
./sbom/scan-running.sh      # fingerprint -> build match -> SPDX + CycloneDX -> grype -> RISK table
```

It reads the boot slot of the flash image your twin booted (and asks the console for
`diag json` to compare), checks which build in this checkout has that fingerprint, and writes
an SBOM in **both** formats with every component labelled **DECLARED** (from
`firmware/platformio.ini`) or **FOUND** (from the image itself: the ESP-IDF version in the app
descriptor, strings that prove mbedTLS / lwIP / FreeRTOS are linked in). If the plant is up it
also SBOMs the operator screen's container (`cs460/hmi-nodered`). grype scans both and the
**RISK** column ranks the findings (EPSS forecast + CISA KEV + severity).

Outputs in `sbom/out/`: `firmware.spdx.json`, `firmware.cdx.json`, `hmi.spdx.json`,
`hmi.cdx.json`, `grype-firmware.txt/.json`, `grype-hmi.txt/.json`, `risk.txt`,
`running-identity.json`, `app.bin` (the boot slot), `cwe-bridge.txt`.

In your notebook, record:

1. **The fingerprint** — does `diag json` agree with the image's boot slot? Which build matched?
2. **SPDX vs CycloneDX** — one concrete difference (field names, how a component is
   identified, how the build-only platform is marked).
3. **One finding with its RISK** and its scope: firmware on the chip, or the HMI container.
4. **What the firmware scan could not see** — the FOUND components with no version.

*Worked contrast (optional, not your device):* `bash sbom/run_sbom.sh` runs the same tools on a
small sample manifest in `sbom/vuln-sample/`.

### Active learning
- **Predict** — before scanning, look at the component list from Exercise 1 and
  write down which of the CVEs you expect to top the grype table are
  **firmware-resident** (code that actually runs on the microcontroller) vs
  **build-host / toolchain-only** (present in the scanned tree but never shipped
  to the chip). This "scope" call is the graded insight.
- **Run** — `./sbom/scan-running.sh`
- **Reconcile** — compare your prediction against the grype table. Being listed
  establishes only that a *component version* carries a known CVE; it does **not**
  establish that the weak code path runs on the device or is reachable. Mark each
  top CVE as firmware-resident or build-host-only, and note what the scan could
  not have told you (reachability).
- **Pair check** — tell a partner, for one CVE, whether it would survive onto the
  StampLC and why.

---

## The lesson: an SBOM only sees what it can parse

Try scanning the firmware source itself:

```bash
syft dir:firmware        # what does it find?
```

You'll get **almost nothing** — a `platformio.ini` of C/C++ library pins is not a
package ecosystem the scanner catalogs, and there's no compiled tree here. That's the
real-world catch:

- **Container / build-host SBOM** (the HMI container in Exercise 2, or the optional sample)
  finds plenty and is where supply-chain attacks like the *XZ* backdoor and *SUNBURST* live —
  but those components don't run on the microcontroller.
- **Firmware-resident SBOM** (mbedTLS, lwIP inside ESP-IDF) is what actually ships on
  the chip. `scan-running.sh` finds them in the image but with **no version**, so you get
  versions by mapping the **framework version → the libraries it bundles → the NVD**, not
  from a one-shot scan.

Say clearly in your finding **which scope** your SBOM covered, and what it therefore
could not have caught.

## The connection:  analyzer ↔ SBOM ↔ CWE ↔ CVE

A scanner finding is a *library* fact: "this component has CVE‑X." Whether it is an
*outcome* depends on two more things — is there **vulnerable code** that reaches the
weak path, and can you **see the result**. The **CWE** is what joins the scanner's CVE
to the firmware's code and to the analyzer's evidence.

`cwe_bridge.py` makes that join from the real scan. `scan-running.sh` (Exercise 2)
already ran it on its grype output and wrote `out/cwe-bridge.txt`. It splits the scanner's CWE classes into two groups:

- **Observable in the twin** — the SBOM found the weakness class in a library *and*
  the firmware has code that reaches it, so the analyzer can show the outcome. From
  the sample scan: `CWE‑295` (no‑TLS transport) → the capture is readable end to end;
  `CWE‑200` (info exposure) → `/api/points` dumps full state; `CWE‑502`
  (deserialization) → a crafted program changes the running logic, visible in the
  I²C analyzer; `CWE‑522` (weak credentials) → the MAC‑derived service password;
  `CWE‑20`/`CWE‑74` (input / injection).
- **Library‑only** — real CVEs in a dependency with **no reachable firmware code / no
  runtime signal here** (e.g. `CWE‑79` template XSS in jinja2, `CWE‑476` null‑deref in
  cryptography). The scanner flags them; the analyzer shows nothing, because nothing
  uses them that way. That gap is the lesson: an SBOM hit is a *question*, not a
  verdict.

**Finding to keep:** take one observable CWE, name the CVE + library the scanner
found, point at the vulnerable code, and attach the analyzer capture that shows the
outcome (e.g. CWE‑319/306 — the cleartext, unauthenticated Modbus coil write in the
`tshark` capture, and `relay0` actuating in `i2c-analyzer.html`). That is the whole
chain in one artifact.
