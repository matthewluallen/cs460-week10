# E4 — An AI anomaly detector on the phone-sensor stream

> Builds on **Part 8 / E3** (phone-as-sensor false-data injection). E3 defended one trusted
> sensor with a *rule* — a plausibility gate you wrote by hand. Here you add the control an OT
> team actually reaches for next: a small **machine-learning anomaly detector** that learns what
> "normal" looks like. Then you attack *it*, the way MITRE's **[ATLAS](https://atlas.mitre.org)**
> framework catalogs attacks on AI systems — because the moment a model makes a decision, the
> model is a new attack surface.

Everything here runs in the Codespace with **no hardware** and, for the core, **no broker** —
the same simulated phone drives the offline verifier and the live MQTT path. ATLAS IDs below are
from the **ATLAS 2026.10** release.

> **Pairs with E5 (red vs blue).** This detector is the blue side. [Extension E5](../caldera/)
> runs the attack as an automated MITRE CALDERA operation — point it at this detector and watch
> the naive spoof get flagged and the crafted one slip past the model to the fusion check.

```bash
pip install -r ai-detector/requirements.txt      # numpy + scikit-learn (+ paho-mqtt, skops, cryptography)
python ai-detector/verify.py                      # prove every property offline (no broker/twin)
```

## The idea

The controller already trusts a phone's tilt (`phone/<id>/orient` β → `phone_tilt_b`, register
146) and alarms when the tilt passes a limit (β > 30°). In E3 you hid a real tilt from the PLC by
spoofing the reported value. Now a detector watches the same stream and raises its own alarm when
a reading does not look like normal operation. **The detector never writes the controller** — it
is an alarm, not a control. Keeping AI out of the actuation path is the point, not a limitation.

There are two build profiles, exactly like the firmware's vulnerable / secure switch:

| | `vuln` | `secure` |
|---|---|---|
| decision | the model alone | an engineering **gate** in front of the model, then the model, then an **accelerometer cross-check** |
| learning | keeps learning online from what it accepts | frozen; only changes through a signed update |
| what it tells a caller | the raw anomaly **score** and the threshold | the **verdict** only, rate-limited, and only if the query endpoint is turned on |
| training data | whatever file you hand it | only a **reviewed, approved** recording; drops frames it can't corroborate |
| model artifact | one `pickle` (code runs on load) | `skops` data + a **signed, hash-pinned** manifest |

## Do it

### 1 · Record a baseline (what "normal" is)
Start the phone bridge (`bash mqtt/share.sh`) and share **both Motion and Orientation** on the
phone page, or simulate a phone:

```bash
python ai-detector/phonesim.py --scenario baseline --out ai-detector/out/baseline.jsonl
# or from your real phone:  python ai-detector/record.py --seconds 300 --out ai-detector/out/baseline.jsonl
```

Whatever is on the wire while you record becomes "normal" to the model. Hold that thought.

### 2 · Train both profiles
```bash
python ai-detector/train.py --data ai-detector/out/baseline.jsonl --profile vuln   --out ai-detector/out/model.pkl
python ai-detector/vendor.py keygen                                                 # one-time signing key
python ai-detector/vendor.py approve-dataset ai-detector/out/baseline.jsonl --by you
python ai-detector/train.py --data ai-detector/out/baseline.jsonl --profile secure --out ai-detector/out/model-signed
```
The secure train **refuses** a dataset you have not approved, and drops readings the
accelerometer can't corroborate before fitting.

### 3 · Watch it live
```bash
python ai-detector/vendor.py publish --registry ai-detector/registry --artifact ai-detector/out/model-signed
python ai-detector/detect.py --profile secure --registry ai-detector/registry      # verdicts on ai/<id>/verdict
# in another shell, be the attacker's phone:
python ai-detector/phonesim.py --scenario crafted-spoof --publish 127.0.0.1:1883 --speed 10
```
(Offline instead of live: add `--replay ai-detector/out/crafted.jsonl` to `detect.py`.)

## Attack it — and read each move as ATLAS

Run `python ai-detector/verify.py` to see every one of these as a PASS/FAIL check, or drive them
live with `phonesim.py --scenario <name>` and `attack.py`.

| What you do | Scenario / tool | ATLAS 2026.10 |
|---|---|---|
| Slam the slider to a wild value | `naive-spoof` | **[AML.T0015](https://atlas.mitre.org/techniques/AML.T0015)** Evade AI Model |
| Hide a real tilt with an **in-distribution** spoof at a normal rate — evades the model alone | `crafted-spoof` | **[AML.T0043.003](https://atlas.mitre.org/techniques/AML.T0043.003)** Craft Adversarial Data: Manual Modification |
| Query the score endpoint to map the boundary | `attack.py probe` | **[AML.T0040](https://atlas.mitre.org/techniques/AML.T0040)** AI Model Inference API Access · **[AML.T0043.001](https://atlas.mitre.org/techniques/AML.T0043.001)** Black-Box Optimization |
| Tilt / cover the real phone | (physical) | **[AML.T0041](https://atlas.mitre.org/techniques/AML.T0041)** Physical Environment Access |
| Publish during the recording so "normal" includes your value | `poisoned-baseline` → `poison-target` | **[AML.T0020](https://atlas.mitre.org/techniques/AML.T0020)** Training Data Poisoning |
| Drift slowly so an online-learning model re-baselines onto you | `erode` | **[AML.T0031](https://atlas.mitre.org/techniques/AML.T0031)** Erode AI Model Integrity |
| Swap / poison the model in the registry, or drop a code-running pickle | `attack.py swap-model` · `evil-pickle` | **[AML.T0010.003](https://atlas.mitre.org/techniques/AML.T0010.003)** Supply Chain Compromise: Model Repository · **[AML.T0018.000](https://atlas.mitre.org/techniques/AML.T0018.000)** Poison AI Model · **[AML.T0011.000](https://atlas.mitre.org/techniques/AML.T0011.000)** Unsafe AI Artifacts |

## Defend it — and read each defence as ATLAS

The `secure` profile is these controls, each one demonstrable (turn it off and watch the matching
attack land):

| Control | What it is here | ATLAS 2026.10 |
|---|---|---|
| **Multi-Sensor Fusion** | cross-check the reported tilt against the tilt gravity implies from the accelerometer; fail closed if the second channel is missing | **[AML.M0009](https://atlas.mitre.org/mitigations/AML.M0009)** |
| **Adversarial Input Detection** | the engineering gate (range + rate envelope) in front of the model | **[AML.M0015](https://atlas.mitre.org/mitigations/AML.M0015)** |
| **Sanitize Training Data** | drop uncorroborated frames before fitting | **[AML.M0007](https://atlas.mitre.org/mitigations/AML.M0007)** |
| **Maintain AI Dataset Provenance** | train only on a hash-approved recording | **[AML.M0025](https://atlas.mitre.org/mitigations/AML.M0025)** |
| **Limit Query Volume & Rate / Output Obfuscation** | verdict only, budgeted, endpoint off by default | **[AML.M0004](https://atlas.mitre.org/mitigations/AML.M0004)** · **[AML.M0002](https://atlas.mitre.org/mitigations/AML.M0002)** |
| **Verify AI Artifacts / Code Signing** | signed, hash-pinned, non-pickle model (see Parts 5–7) | **[AML.M0014](https://atlas.mitre.org/mitigations/AML.M0014)** · **[AML.M0013](https://atlas.mitre.org/mitigations/AML.M0013)** |

## The honest residual risk (say it in your finding)

The accelerometer cross-check compares two channels **from the same phone**. An attacker who
forges *both* (`forge-all`) defeats it — because real fusion needs an **independent** sensor, not
a second reading from the same source. This is the same lesson as E3's "name an out-of-band
cross-check," now with a model in the loop: a detector adds work for the attacker and buys
detection, but it does not make a single unauthenticated sensor trustworthy. Every value entering
control logic is still a claim, not a fact — and now so is the detector's verdict.

## What to hand in (Part-standard finding)

1. **Baseline + model:** what you recorded as "normal," and the detector's threshold.
2. **One evasion:** a spoof the model alone misses (`crafted-spoof`), with the wire capture, and
   the lens/channel that catches it (fusion) — say which is observation and which is inference.
3. **One integrity attack on the model itself:** poisoning *or* erosion *or* a registry swap, and
   the control that stops it in the `secure` profile.
4. **One sentence** tying the detector's verdict back to the Signal Journey: it is one more claim,
   kept out of the actuation path.

## Files
`aidet.py` (shared library) · `scenarios.py` (named normal/attack streams) · `phonesim.py`
(simulate/publish a phone) · `record.py` (record your real phone) · `train.py` · `detect.py`
(live/replay + `/score`) · `vendor.py` (keys, dataset approval, publish) · `attack.py` (registry
+ oracle attacks) · `aibom.py` (AI-BOM, Part 5) · `verify.py` (the E4 anchor). The design notes —
including why a Gaussian-mixture density, not an Isolation Forest — are at the top of `aidet.py`.
