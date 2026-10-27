# Part 2 — Software Bill of Materials (SBOM)

An **SBOM** lists what's *inside the software*: the libraries, packages and versions
a build pulls in. Security teams use it to answer "do we ship anything with a known
CVE?" You'll generate SBOMs in two standard formats, compare them, scan one for
vulnerabilities, and — the honest part — see where an SBOM's view stops.

Tools (installed for you): **syft** (generates SBOMs), **grype** (scans an SBOM for
CVEs), **trivy** (does both), and **cve-bin-tool** (scans a *compiled binary* for
embedded components + CVEs — Exercise 3).

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

## Exercise 2 — generate, compare, scan

Run the helper (or the commands by hand):

```bash
bash sbom/run_sbom.sh
```

It generates the same component set as **SPDX** and as **CycloneDX**, then scans with
grype. In your notebook, record:

1. **SPDX vs CycloneDX** — one concrete difference you see (a field name, how components
   are identified, the component count). They describe the same thing in different schemas.
2. **Top 5 CVEs** from the grype table (CVE, severity, component, version) for the report.
3. Pick **one** CVE, find it in the NVD, and summarize its cause in a sentence.

By hand, the key commands are:

```bash
syft dir:vuln-sample -o spdx-json      > out/sbom.spdx.json   # SPDX
syft dir:vuln-sample -o cyclonedx-json > out/sbom.cdx.json    # CycloneDX
grype sbom:out/sbom.spdx.json -o table                        # CVEs
```

---

## The lesson: an SBOM only sees what it can parse

Try scanning the firmware source itself:

```bash
syft dir:firmware        # what does it find?
```

You'll get **almost nothing** — a `platformio.ini` of C/C++ library pins is not a
package ecosystem the scanner catalogs, and there's no compiled tree here. That's the
real-world catch:

- **Build-host / toolchain SBOM** (what Exercise 2 scans) finds plenty and is where
  supply-chain attacks like *XZ* and *SolarWinds* live — but those components don't all
  run on the microcontroller.
- **Firmware-resident SBOM** (mbedTLS, lwIP inside ESP-IDF) is what actually ships on
  the chip, and you usually get it by mapping the **framework version → the libraries it
  bundles → the NVD**, not from a one-shot scan.

Say clearly in your finding **which scope** your SBOM covered, and what it therefore
could not have caught.

## Exercise 3 — scan the *compiled binary* (the firmware-resident view)

Exercise 2 read build *manifests*. Now read the shipped artifact itself. **cve-bin-tool**
(installed for you) scans a compiled binary for the version fingerprints of well-known
components and maps them to CVEs. The interesting part is what it does — and does **not** —
find in this firmware.

```bash
bash sbom/run_cve_bin_tool.sh        # scans firmware/prebuilt/stamplc-factory-8MB.bin
```

The script uses cve-bin-tool's **json-mirror** data source (a public CVE snapshot pulled with
`gsutil`), so it needs **no NVD API key** and works even when `nvd.nist.gov` is down or
rate-limited. The dev container installs `gsutil` and pre-caches the mirror on build, so the
first scan is quick. (If you have an NVD key and want the live feed instead:
`cve-bin-tool <bin> --nvd-api-key <key>`.)

Even with no network, you can confirm the components are really in there:

```bash
strings sbom/firmware/prebuilt/stamplc-factory-8MB.bin | grep -iE 'mbedtls|lwip|wpa_supplicant|esp-idf'
```

You'll see `mbedTLS`, `lwIP` and `wpa_supplicant` paths — the crypto and TCP/IP stacks the
note above said "most *embedded* CVEs actually live" in.

**Now compare that to what cve-bin-tool reports: on this image it finds `0 products with known
CVEs`.** That is not a bug and not a clean bill of health — it *is* the lesson. cve-bin-tool
matches by *version fingerprints*, and the ESP-IDF monolithic build does not expose the bundled
libraries' versions in a form its checkers recognize — even though `strings` proves the code is
right there. So:

- **Source SBOM** (syft on `firmware/`) → the build's library *pins*, little CVE signal.
- **Binary scan** (cve-bin-tool on the `.bin`) → here, **0 detections** — a dedicated binary
  scanner still misses firmware-resident components when their versions aren't fingerprintable.
- **The real firmware-resident view** comes from mapping the **framework version → the libraries
  it bundles → the NVD** by hand (the Exercise-1 method), not from any one-shot scan.

**Finding to keep:** report cve-bin-tool's result for this binary (how many products / CVEs it
detected — likely **0**), reconcile it against the `strings` evidence that the components ARE
present, and state the lesson in one sentence: **absent from the scan is not absent from the
device.** Then take one component you know is in the firmware (from Exercise 1), look its version
up in the NVD yourself, and say whether a fix would be a firmware rebuild or an architecture
control (detected ≠ exploitable — and *not*-detected ≠ *not*-present).

## The connection:  analyzer ↔ SBOM ↔ CWE ↔ CVE

A scanner finding is a *library* fact: "this component has CVE‑X." Whether it is an
*outcome* depends on two more things — is there **vulnerable code** that reaches the
weak path, and can you **see the result**. The **CWE** is what joins the scanner's CVE
to the firmware's code and to the analyzer's evidence.

`cwe_bridge.py` makes that join from the real scan:

```bash
grype dir:vuln-sample -o json > out/grype.json
python3 cwe_bridge.py out/grype.json --all
```

It splits the scanner's CWE classes into two groups:

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
