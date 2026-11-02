#!/usr/bin/env python3
"""ai-detector/scenarios.py — named phone-stream scenarios (normal operation and attacks).

Each scenario returns the MQTT messages a phone would send: (t, id, topic, payload). The SAME
scenarios drive the offline verifier (verify.py) and the live fake phone (phonesim.py --publish),
so what the verifier proves is what you can watch happen on the broker.

The narrative (Part 8 / E3): a phone's tilt β feeds the controller, and the rung alarms when
β > 30°. The attacker's goal is to HIDE a real alarm — keep reporting a normal-looking tilt
while the phone is really tipped past the limit.
"""
import math

from aidet import PhoneSim

ALARM_LIMIT = 30.0


def _hide_alarm(release=27.0, target=15.0, rate=1.0):
    """Crafted spoof (AML.T0043.003 Manual Modification): follow the real tilt until it nears
    the limit, then slide the reported value gently (1°/s, a normal rate) back to the most
    typical tilt and stay there — every reported value is one the model has seen many times."""
    st = {}

    def report(t, b, g):
        if b < release and "t0" not in st:
            return b, g
        st.setdefault("t0", (t, b))
        t0, b0 = st["t0"]
        return max(target, b0 - rate * (t - t0)), g
    return report


def _jump(value=-120.0, start=30.0, stop=40.0):
    """Naive spoof: drag the phone page's Override slider straight to an extreme value."""
    return lambda t, b, g: (value, g) if start <= t < stop else (b, g)


def _poison(target=-30.0, start=200.0, ramp=15.0, hold=90.0):
    """Training-data poisoning (AML.T0020): while the baseline is being recorded, hold the
    reported tilt in a region the attacker wants the model to call normal later."""
    def report(t, b, g):
        if start <= t < start + ramp:
            return b + (target - b) * (t - start) / ramp, g
        if start + ramp <= t < start + ramp + hold:
            return target + 2.0 * math.sin(t / 3.0), g
        if start + ramp + hold <= t < start + 2 * ramp + hold:
            return target + (b - target) * (t - start - ramp - hold) / ramp, g
        return b, g
    return report


def _drift(rate=0.05, floor=-30.0):
    """Erosion (AML.T0031): walk the reported centre down slowly, keeping the real wander,
    so an online-learning model accepts each small step and re-baselines onto it."""
    return lambda t, b, g: (max(floor, 15.0 - rate * t) + (b - 15.0), g)


def build(name, seed=0, phone_id="sim1"):
    """Return (messages, info) for a named scenario."""
    sim = PhoneSim(seed=seed)
    if name == "baseline":                       # normal operation incl. real tilt events
        return sim.messages(sim.track(600), phone_id), {"seconds": 600}
    if name == "legit":                          # a REAL tilt past the limit — must not be hidden
        tr = sim.track(90, events=[(10, 46, 60)])
        return sim.messages(tr, phone_id), {"seconds": 90, "real_peak": 46}
    if name == "naive-spoof":
        tr = sim.track(90, events=[(10, 46, 60)])
        return sim.messages(tr, phone_id, report=_jump()), {"window": (30, 40)}
    if name == "crafted-spoof":                  # hide the real alarm; accel tells the truth
        tr = sim.track(90, events=[(10, 46, 60)])
        return sim.messages(tr, phone_id, report=_hide_alarm()), {"real_peak": 46}
    if name == "withhold-accel":                 # crafted spoof + stop sending the corroborating channel
        tr = sim.track(90, events=[(10, 46, 60)])
        return sim.messages(tr, phone_id, report=_hide_alarm(), accel=False), {}
    if name == "forge-all":                      # crafted spoof + forge accel to match (one source, both channels)
        tr = sim.track(90, events=[(10, 46, 60)])
        return sim.messages(tr, phone_id, report=_hide_alarm(), forge_accel=True), {}
    if name == "poisoned-baseline":              # a baseline recorded while the attacker held -30°
        return sim.messages(sim.track(600), phone_id, report=_poison()), {"target": -30.0}
    if name == "poison-target":                  # later: report -30° and expect to pass
        tr = sim.track(60, excursions=False)
        rep = lambda t, b, g: (-30.0 + 1.5 * math.sin(t / 3.0), g) if t >= 20 else (b, g)
        return sim.messages(tr, phone_id, report=rep), {"window": (40, 60), "target": -30.0}
    if name == "erode":                          # ~17 minutes of slow drift to -30°
        secs = 1020
        tr = sim.track(secs, excursions=False)
        return sim.messages(tr, phone_id, report=_drift()), {"seconds": secs, "target": -30.0}
    raise KeyError(name)


NAMES = ["baseline", "legit", "naive-spoof", "crafted-spoof", "withhold-accel", "forge-all",
         "poisoned-baseline", "poison-target", "erode"]
