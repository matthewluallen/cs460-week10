# 01 — Master item index (Stage 1)

Every reviewable item in the project. `anchor` = the machine-checkable test that proves it, or `none`
(a Stage-2 G5 gap). This index is v1 from the repo at merge time; we refine per slice.

## A. Lab steps (core, Parts 1–9)
| ID | Part | Where | Parity | Verifier anchor |
|---|---|---|---|---|
| P1 | Wire & run (input→logic→output, fail-safe motor) | `wiring/` | both | `verify-lab.py P1` |
| P2 | Ladder logic (change one rung, prove installed) | `ladder/` | both | `verify-lab.py P2` |
| P3 | Bus & network (I²C tap + Modbus/MQTT on the wire) | `bus-analysis/`, `stations/` | twin | `verify-lab.py P3` |
| P4 | HBOM (parts list vs live bus) | `hbom/` | both | `verify-lab.py P4` |
| P5 | SBOM & firmware image (declared vs found) | `sbom/`, `firmware-re/` | offline/twin | `verify-lab.py P5` |
| P6 | Unsigned OTA | `firmware-ota/` | twin | `verify-lab.py P6` |
| P7 | Supply-chain kill switch | `supply-chain/` | twin | `verify-lab.py P7` (**manual SKIP — G5**) |
| P8 | Untrusted inputs: NTP/Y2038 + phone FDI | `ntp-aitm/`, `mqtt/` | twin | `verify-lab.py P8` (phone half not anchored — G5) |
| P9 | Harden (prove each fix, name residual) | `remediation/` | twin | `verify-lab.py P9`, `verify-secure.py` |

## B. Extensions & Week 12
| ID | Name | Where | Anchor |
|---|---|---|---|
| E1 | Provenance Studio (event → source line + CWE; the provenance gap) | `web/public/provenance_studio.html` | none (G5) |
| E2 | Signal Journey (one value across 5 lenses) | `web/public/signal_journey.html` | none (G5) |
| E3 | Phone-as-sensor false-data injection (availability + integrity + plausibility gate) | `docs/assignment-phone-sensor-fdi.html` | none (graded prose) |
| W12 | Formal verification (Z3 proofs; verified update pipeline) | `formal-verify/`, `week12/` | `verify_controls.py` |

## C. Firmware weaknesses (the demonstrated vulnerabilities)
| ID | Weakness | CWE | Twin-observable? |
|---|---|---|---|
| FW1 | Default web creds `admin:openplc` | CWE-798/1392 | recovered from image |
| FW2 | Hardcoded OTA HMAC signing key | CWE-321 | string in image |
| FW3 | Unauthenticated Modbus/TCP coil write | CWE-306 | yes (relay) |
| FW4 | Modbus spoof / reporting message | CWE-345/940 | yes |
| FW5 | MQTT (no broker auth) → point injection | CWE-306/862 | yes |
| FW6 | Unsigned OTA accepted | CWE-345/347 | `verified=NO` |
| FW7 | Supply-chain dormant payload → relay | CWE-506/494 | yes (relay, no input) |
| FW8 | NTP/Y2038 32-bit wrap | CWE-197 (also 190/191) | yes (clk→1902) |
| FW9 | Memory overflow → force mask (audit bypass) | CWE-787/788 | yes (relay, no log row) |
| FW10 | OS command injection | CWE-78 | **no shell — pattern-only (G9 risk)** |

## D. Web tools
| ID | Tool | Where | Anchor |
|---|---|---|---|
| T1 | Logic Studio (author/monitor + circuit designer) | `web/public/logic_studio.html`, `circuits/` | `studio-regression.js`, `circuit-*.js` |
| T2 | Threat Map (7-framework cross-walk) | `web/public/threat-map.html` | none (G5) |
| T3 | Provenance Studio | `web/public/provenance_studio.html` | none |
| T4 | Signal Journey | `web/public/signal_journey.html` | none |
| T5 | Phone sensor page (+ trend graph, send-rate) | `mqtt/public/index.html` | `test-phone-points.py` |
| T6 | Operator panel | `web/public/panel.html` | none |

## E. Scripts / harnesses
| ID | Script | Role |
|---|---|---|
| S1 | `assessment/verify-lab.py` | per-part observable checks (P1–P9) |
| S2 | `remediation/verify-secure.py` | re-run attacks on the hardened build |
| S3 | `qemu/` (run, studio, twin-bridge, smoke-test) | the twin + Studio + 21-check smoke |
| S4 | `bus-analysis/` (tap.sh, decode.js) | I²C capture + Modbus decode |
| S5 | `sbom/` (run_sbom.sh, scan-running.sh, cwe_bridge.py) | SBOM + CVE scan + reachability |
| S6 | `firmware-re/analyze.sh` | image RE (strings/binwalk/objdump) |
| S7 | `firmware-ota/push-unsigned.sh` | the unsigned-OTA probe |
| S8 | `supply-chain/` (c2_server, run-malicious, diff-images, arm) | the kill-switch exercise |
| S9 | `ntp-aitm/` (ntp_server, arm) | the rogue time server |
| S10 | `stations/` (docker-compose + plc/hmi/engineer/capture/mqtt) | the plant network |
| S11 | `tools/` (studio-regression, circuit-catalog-check, circuit-live-selftest, gen-provenance-map) | web-tool tests + provenance |
| S12 | `formal-verify/verify_controls.py` | Z3 proofs (Week 12) |

## F. Data artifacts
| ID | Artifact | Where |
|---|---|---|
| D1 | HBOM | `hbom/stamplc-hbom.csv` |
| D2 | SBOMs (SPDX + CycloneDX, firmware + HMI) | generated to `sbom/out/` |
| D3 | Firmware provenance map (source↔address) | `web/public/firmware-provenance-map.json` |

## G. Assessment & docs
| ID | Item | Where | Note |
|---|---|---|---|
| A1 | Graded video assignment (1–2 connected Parts) | Canvas | rubric partly outside repo |
| A2 | E3 phone-FDI assignment | `docs/assignment-phone-sensor-fdi.html` | graded prose |
| A3 | Week 12 assignment | `week12/` | — |
| A4 | Learning objectives (measurable) | `assessment/objectives.md` | source of truth #1 |
| DOC1 | Lab page (Sections A–D, nine Parts) | Canvas / `web` fragments | source of truth #3 |
| DOC2 | README, StampLC-User-Manual, QEMU-Twin | `README.md`, `docs/` | — |

**Counts (v1):** 9 core Parts · 4 extension/W12 · 10 firmware weaknesses · 6 web tools · 12 script groups ·
3 data artifacts · 4 assessment + 2 doc sets. Blanks in `anchor` = Stage-2 G5 candidates.
