#!/usr/bin/env python3
"""ai-detector/verify.py — the E4 machine-checkable anchor (offline, no broker, no twin).

Trains both detector profiles on a simulated baseline and asserts, for each scenario, the
property the lab teaches — the "mechanics" half of the rubric, the reasoning is the student's
finding. Run it directly or through assessment/verify-lab.py E4.

    python ai-detector/verify.py              # PASS/FAIL per check
    python ai-detector/verify.py --json       # machine-readable

Every check maps to an ATLAS technique or mitigation (ATLAS 2026.10, atlas.mitre.org). It is a
self-contained property test: it builds its own artifacts in a temp dir, so it needs only
numpy + scikit-learn (+ skops & cryptography for the signed-artifact checks; those SKIP if absent).
"""
import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aidet  # noqa: E402
import scenarios  # noqa: E402

PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"


def _alarm_rate(det, profile, msgs, sel=None):
    frames = aidet.frames_from_messages(msgs)
    pairs = [(f, v) for f, v in zip(frames, det.evaluate_many(frames, profile))
             if sel is None or sel(f)]
    if not pairs:
        return 0.0, [], 0
    alarms = sum(v.verdict == "ALARM" for _, v in pairs)
    stages = sorted({r.split(":")[0] for _, v in pairs for r in v.reasons})
    return alarms / len(pairs), stages, len(pairs)


def run():
    checks = []

    def record(cid, atlas, verdict, note):
        checks.append({"id": cid, "atlas": atlas, "verdict": verdict, "note": note})

    base, _ = scenarios.build("baseline", seed=1)
    frames = aidet.frames_from_messages(base)
    det_v = aidet.train(frames, "vuln", seed=0)
    det_s = aidet.train(frames, "secure", seed=0)

    # --- baseline false-alarm rate: both profiles leave normal operation alone
    held, _ = scenarios.build("baseline", seed=7)
    rv, _, nv = _alarm_rate(det_v, "vuln", held)
    rs, _, _ = _alarm_rate(det_s, "secure", held)
    record("E4-00-baseline-fpr", "—",
           PASS if rv < 0.05 and rs < 0.05 else FAIL,
           f"held-out normal operation alarms: vuln {rv:.1%}, secure {rs:.1%} (both < 5%)")

    # --- a REAL tilt past the limit must NOT be hidden by either profile
    legit, info = scenarios.build("legit", seed=3)
    rv, _, _ = _alarm_rate(det_v, "vuln", legit, sel=lambda f: f.beta > 35)
    rs, _, _ = _alarm_rate(det_s, "secure", legit, sel=lambda f: f.beta > 35)
    record("E4-01-legit-passes", "—",
           PASS if rv < 0.1 and rs < 0.1 else FAIL,
           f"a real {info['real_peak']}° tilt is reported honestly and is not flagged as anomalous "
           f"(vuln {rv:.1%}, secure {rs:.1%})")

    # --- naive spoof (slider to -120): model catches it; secure gate catches it first
    naive, _ = scenarios.build("naive-spoof", seed=3)
    rv, sv, _ = _alarm_rate(det_v, "vuln", naive, sel=lambda f: f.beta < -100)
    rs, ss, _ = _alarm_rate(det_s, "secure", naive, sel=lambda f: f.beta < -100)
    record("E4-02-naive-spoof-caught", "AML.T0015",
           PASS if rv > 0.9 and rs > 0.9 else FAIL,
           f"an out-of-envelope spoof is flagged: vuln {rv:.0%} ({'+'.join(sv) or '-'}), "
           f"secure {rs:.0%} ({'+'.join(ss) or '-'})")

    # --- crafted spoof (hide a real alarm, in-distribution, normal rate)
    crafted, _ = scenarios.build("crafted-spoof", seed=3)
    rv, sv, _ = _alarm_rate(det_v, "vuln", crafted, sel=lambda f: f.beta < 20)
    record("E4-03-evade-model-only", "AML.T0043.003",
           PASS if rv < 0.25 else FAIL,
           f"a crafted in-distribution spoof EVADES the model alone (vuln flags only {rv:.0%} "
           f"of the hidden window — evasion is the point of this step)")
    rs, ss, _ = _alarm_rate(det_s, "secure", crafted, sel=lambda f: f.beta < 20)
    record("E4-04-fusion-catches-evasion", "AML.M0009",
           PASS if rs > 0.5 and "fusion" in ss else FAIL,
           f"the accelerometer cross-check catches the same spoof: secure flags {rs:.0%} "
           f"({'+'.join(ss) or '-'})")

    # --- withholding the corroborating channel fails closed
    wh, _ = scenarios.build("withhold-accel", seed=3)
    rs, ss, _ = _alarm_rate(det_s, "secure", wh, sel=lambda f: f.beta < 20)
    record("E4-05-fusion-fail-closed", "AML.M0009",
           PASS if rs > 0.9 and "fusion" in ss else FAIL,
           f"withholding the accelerometer does not buy silence — uncorroborated reads alarm "
           f"({rs:.0%} in the hidden window)")

    # --- forging BOTH channels from one phone defeats same-phone fusion (honest residual risk)
    fa, _ = scenarios.build("forge-all", seed=3)
    rs, _, _ = _alarm_rate(det_s, "secure", fa, sel=lambda f: f.beta < 20)
    record("E4-06-forge-all-residual", "AML.T0043.003",
           PASS if rs < 0.25 else FAIL,
           f"forging accel to match the spoof defeats same-phone fusion ({rs:.0%} flagged) — "
           f"residual risk: fusion needs an INDEPENDENT sensor, not a second channel from the same source")

    # --- training-data poisoning: a baseline recorded while holding -30° normalizes -30°
    pbase, _ = scenarios.build("poisoned-baseline", seed=11)
    pframes = aidet.frames_from_messages(pbase)
    det_pv = aidet.train(pframes, "vuln", seed=0)
    tgt, _ = scenarios.build("poison-target", seed=12)
    rv_clean, _, _ = _alarm_rate(det_v, "vuln", tgt, sel=lambda f: f.t > 40)
    rv_pois, _, _ = _alarm_rate(det_pv, "vuln", tgt, sel=lambda f: f.t > 40)
    record("E4-07-poison-training", "AML.T0020",
           PASS if rv_clean > 0.9 and rv_pois < 0.1 else FAIL,
           f"a -30° reading alarms on a clean model ({rv_clean:.0%}) but is accepted after the model "
           f"was trained on a poisoned baseline ({rv_pois:.0%})")

    # --- secure training sanitizes the poisoned baseline before fitting (AML.M0007)
    try:
        det_ps = aidet.train(pframes, "secure", seed=0)
        dropped = det_ps.meta["dropped_frames"]
        rs_pois, _, _ = _alarm_rate(det_ps, "secure", tgt, sel=lambda f: f.t > 40)
        record("E4-08-sanitize-training", "AML.M0007",
               PASS if dropped > 500 and rs_pois > 0.9 else FAIL,
               f"the secure profile drops {dropped} uncorroborated frames before fitting, so -30° still "
               f"alarms ({rs_pois:.0%})")
    except aidet.AidetError as e:
        record("E4-08-sanitize-training", "AML.M0007", FAIL, f"secure training errored: {e}")

    # --- online learning erodes a frozen-vs-adaptive model differently (AML.T0031)
    erode, einfo = scenarios.build("erode", seed=13)
    emsgs = erode
    efr = aidet.frames_from_messages(emsgs)
    tail = lambda f: f.t > einfo["seconds"] - 60
    _, _, _ = _alarm_rate(det_v, "vuln", emsgs)          # frozen model (fresh instance below for clarity)
    det_frozen = aidet.train(frames, "vuln", seed=0)
    rf, _, _ = _alarm_rate(det_frozen, "vuln", emsgs, sel=tail)
    det_adapt = aidet.train(frames, "vuln", seed=0)
    det_adapt.enable_adapt(seed_frames=frames, window=3000, every=200)
    va = det_adapt.evaluate_many(efr, "vuln")
    tailpairs = [(f, v) for f, v in zip(efr, va) if tail(f)]
    ra = sum(v.verdict == "ALARM" for _, v in tailpairs) / max(1, len(tailpairs))
    record("E4-09-erode-online-learning", "AML.T0031",
           PASS if rf > 0.9 and ra < 0.5 else FAIL,
           f"a slow drift to {einfo['target']}° keeps the frozen model alarming ({rf:.0%}) but an "
           f"online-learning model re-baselines onto it ({ra:.0%} by the end; {det_adapt.adapt['refits']} refits)")

    # --- supply chain: the signed/verified loader rejects what the pickle loader accepts
    try:
        import skops.io  # noqa: F401
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey  # noqa: F401
        have_crypto = True
    except Exception:
        have_crypto = False
    if not have_crypto:
        for cid, atlas in (("E4-10-unsigned-rejected", "AML.M0013"),
                           ("E4-11-tamper-rejected", "AML.M0014"),
                           ("E4-12-no-pickle-exec", "AML.T0011.000")):
            record(cid, atlas, SKIP, "skops / cryptography not installed (pip install skops cryptography)")
    else:
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["AIDET_HOME"] = tmp
            trust = os.path.join(tmp, "trust")
            vendor = os.path.join(tmp, "vendor")
            aidet.keygen(vendor, trust, force=True)
            signed = os.path.join(tmp, "signed")
            aidet.save_signed(det_s, signed, key_path=os.path.join(vendor, "vendor.key"), version="1.0.0")

            # 10: unsigned artifact (a bare model.pkl) is refused by the secure loader
            reg = os.path.join(tmp, "registry")
            aidet.publish(reg, aidet.save_pickle(det_s, os.path.join(tmp, "m.pkl")))
            try:
                aidet.load_from_registry(reg, "secure", os.path.join(trust, "vendor.pub"))
                record("E4-10-unsigned-rejected", "AML.M0013", FAIL, "secure loader accepted an unsigned pickle")
            except aidet.AidetError:
                record("E4-10-unsigned-rejected", "AML.M0013", PASS,
                       "secure loader refuses an unsigned artifact; the vuln loader would load it")

            # 11: tamper the signed model.json -> hash mismatch -> refused
            aidet.publish(reg, signed)
            cur = aidet.registry_current(reg)
            st = json.load(open(os.path.join(cur, "model.json")))
            st["threshold"] = -1e9
            json.dump(st, open(os.path.join(cur, "model.json"), "w"))
            try:
                aidet.load_signed(cur, os.path.join(trust, "vendor.pub"))
                record("E4-11-tamper-rejected", "AML.M0014", FAIL, "tampered model.json loaded anyway")
            except aidet.AidetError:
                record("E4-11-tamper-rejected", "AML.M0014", PASS,
                       "editing the signed model breaks its SHA-256 and the loader refuses it")

            # 12: a pickle that runs code on load — vuln executes it, secure never unpickles it
            marker = os.path.join(tmp, "ran")

            class RunsOnLoad:
                def __reduce__(self):
                    return (open, (marker, "w"))
            import pickle
            aidet.publish(reg, signed)
            cur = aidet.registry_current(reg)
            with open(os.path.join(cur, "model.pkl"), "wb") as f:
                pickle.dump({"x": RunsOnLoad()}, f)
            vuln_ran = False
            try:
                pickle.load(open(os.path.join(cur, "model.pkl"), "rb"))   # what the vuln loader does
            except Exception:
                pass
            vuln_ran = os.path.exists(marker)
            if os.path.exists(marker):
                os.unlink(marker)
            try:
                aidet.load_signed(cur, os.path.join(trust, "vendor.pub"))
            except aidet.AidetError:
                pass
            secure_ran = os.path.exists(marker)
            record("E4-12-no-pickle-exec", "AML.T0011.000",
                   PASS if vuln_ran and not secure_ran else FAIL,
                   f"loading a crafted pickle runs attacker code (vuln path: ran={vuln_ran}); the secure "
                   f"loader never unpickles, so no code runs (secure path: ran={secure_ran})")
            os.environ.pop("AIDET_HOME", None)

    return checks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    checks = run()
    if a.json:
        print(json.dumps(checks, indent=2))
    else:
        print("== E4 — AI anomaly detector on the phone-sensor stream ==\n")
        for c in checks:
            tag = f"[{c['atlas']}]" if c["atlas"] != "—" else ""
            print(f"  [{c['verdict']}] {c['id']:28} {tag}\n          {c['note']}")
    nfail = sum(c["verdict"] == FAIL for c in checks)
    if not a.json:
        npass = sum(c["verdict"] == PASS for c in checks)
        nskip = sum(c["verdict"] == SKIP for c in checks)
        print(f"\n== {npass} PASS · {nfail} FAIL · {nskip} SKIP ==")
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
