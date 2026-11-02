#!/usr/bin/env python3
"""ai-detector/aidet.py — E4: an AI anomaly detector on the phone-sensor stream (shared library).

The detector watches the tilt point the controller consumes (``phone/<id>/orient`` β/γ →
``phone_tilt_b`` / ``phone_tilt_g``, registers 146/147) and returns OK or ALARM for every reading.
It is deliberately **out of the actuation path**: it raises an alarm; it never writes the PLC.

Two profiles, mirroring the firmware's vulnerable / secure build switch:

  vuln    the model alone · raw anomaly score exposed · learns online (re-baselines on what it
          accepted) · trains on whatever recording it is handed · loads ``model.pkl`` from the
          model registry with pickle, unverified
  secure  an engineering gate in front of the model (AML.M0015) · fusion with the accelerometer,
          fail-closed (AML.M0009) · frozen model · verdict only, query budget, oracle off by
          default (AML.M0002 / AML.M0004 / AML.M0019) · trains only on an approved recording and
          drops frames the fusion check rejects (AML.M0025 / AML.M0007) · loads only a signed,
          hash-pinned, non-executable artifact (AML.M0013 / AML.M0014 / AML.M0011)

Every script in this folder imports this one module, so the live classroom path, the attack
scripts and the offline verifier all run the same code. ATLAS IDs are from the ATLAS 2026.10
release (atlas.mitre.org).
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import pickle
import shutil
import time
from collections import deque
from dataclasses import dataclass, field

import numpy as np

G = 9.80665
FEATURES = ("beta", "gamma", "dbeta", "dgamma")      # deg, deg, deg/s, deg/s
FEATURE_FLOOR = np.array([1.0, 1.0, 0.5, 0.5])       # std floor so a near-constant feature can't explode
ACCEL_MAX_AGE = 2.5        # s — the phone page re-sends every topic at least every 2 s
SCORE_QUANTILE = 0.002     # threshold = this quantile of the training scores (~0.2 % baseline alarms)
SKOPS_TRUSTED: list = []        # extra types the secure loader may load: none (the model is plain arrays)
DEFAULT_GATE = {"beta": [-90.0, 90.0], "gamma": [-90.0, 90.0], "max_rate_dps": 90.0}
HOME = os.path.expanduser(os.environ.get("AIDET_HOME", "~/.cs460-ai"))
TRUST_DIR = os.environ.get("AIDET_TRUST", os.path.join(HOME, "trust"))       # public key + approved datasets
VENDOR_DIR = os.environ.get("AIDET_VENDOR", os.path.join(HOME, "vendor"))    # the vendor's PRIVATE key
HERE = os.path.dirname(os.path.abspath(__file__))


class AidetError(Exception):
    """A refusal: the secure profile declining to train on, load, or answer something."""


# ------------------------------------------------------------------------------------ messages
# A message is (t, phone_id, topic, payload) — exactly what arrives on phone/<id>/<topic>.

def write_jsonl(path, msgs):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        for t, pid, topic, p in msgs:
            f.write(json.dumps({"t": round(t, 3), "id": pid, "topic": topic, "payload": p}) + "\n")


def read_jsonl(path):
    out = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                m = json.loads(line)
                out.append((float(m["t"]), m["id"], m["topic"], m["payload"]))
    return out


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def angdiff(a, b):
    """Signed a-b in degrees, wrapped to [-180, 180)."""
    return (a - b + 180.0) % 360.0 - 180.0


# ------------------------------------------------------------------------------------ simulator
class PhoneSim:
    """A physically consistent phone: ONE true attitude drives both channels.

    orient = the attitude the phone reports (W3C DeviceOrientation β, γ in degrees);
    accel  = gravity seen in the phone frame (accelerationIncludingGravity, W3C axes):
             g · (−sin γ·cos β,  sin β,  cos γ·cos β)   — flat & face-up reads (0, 0, +g).
    A spoof replaces what orient REPORTS; unless the attacker also forges accel, accel keeps
    telling the truth. That gap is what multi-sensor fusion (AML.M0009) measures.
    """

    def __init__(self, seed=0, hz=10.0):
        self.rng = np.random.default_rng(seed)
        self.hz = hz

    def track(self, seconds, start=15.0, excursions=True, events=None):
        """True attitude over time: β wanders around 15° (a propped sensor) with real tilt events —
        ramps to 42–50°, held 10–40 s, and back — that the PLC is meant to alarm on (β > 30).
        events=[(t_start, peak_deg, hold_s), ...] scripts the events instead of drawing them."""
        n = int(seconds * self.hz)
        dt = 1.0 / self.hz
        beta, gamma = start, 0.0
        out = []
        script = sorted(events or [])
        if events is not None:
            next_ev = script[0][0] if script else 1e12
        else:
            next_ev = self.rng.uniform(25, 45) if excursions else 1e12
        ev = None                                          # [phase, peak, rate, hold_until, hold_s]
        for i in range(n):
            t = i * dt
            if ev is None and t >= next_ev:
                if events is not None:
                    _, peak, hold_s = script.pop(0)
                else:
                    peak, hold_s = self.rng.uniform(42, 50), self.rng.uniform(10, 40)
                ev = ["up", peak, self.rng.uniform(4, 8), 0.0, hold_s]
            if ev is not None:
                phase, peak, rate, hold_until, hold_s = ev
                if phase == "up":
                    beta = min(peak, beta + rate * dt)
                    if beta >= peak:
                        ev[0], ev[3] = "hold", t + hold_s
                elif phase == "hold":
                    beta += 0.6 * math.sqrt(dt) * self.rng.standard_normal()
                    if t >= hold_until:
                        ev[0] = "down"
                else:
                    beta = max(15.0, beta - rate * dt)
                    if beta <= 15.0:
                        ev = None
                        if events is not None:
                            next_ev = script[0][0] if script else 1e12
                        else:
                            next_ev = t + self.rng.uniform(40, 70)
            else:                                          # Ornstein–Uhlenbeck wander
                beta += 0.25 * (15.0 - beta) * dt + 1.8 * math.sqrt(dt) * self.rng.standard_normal()
            gamma += 0.3 * (0.0 - gamma) * dt + 0.9 * math.sqrt(dt) * self.rng.standard_normal()
            out.append((t, beta, gamma))
        return out

    @staticmethod
    def accel_of(beta, gamma):
        b, g = math.radians(beta), math.radians(gamma)
        return (-G * math.sin(g) * math.cos(b), G * math.sin(b), G * math.cos(g) * math.cos(b))

    def messages(self, track, phone_id="sim1", report=None, accel=True, forge_accel=False, t0=0.0):
        """Turn a true track into the MQTT messages the phone page would send.

        report(t, beta, gamma) -> (beta', gamma') is what orient REPORTS (default: the truth).
        accel=False withholds the accelerometer; forge_accel=True makes it match the report."""
        msgs = []
        for t, beta, gamma in track:
            rb, rg = report(t, beta, gamma) if report else (beta, gamma)
            rb += 0.2 * self.rng.standard_normal()
            rg += 0.2 * self.rng.standard_normal()
            msgs.append((t0 + t, phone_id, "orient",
                         {"alpha": 0.0, "beta": round(rb, 2), "gamma": round(rg, 2)}))
            if accel:
                ab, ag = (rb, rg) if forge_accel else (beta, gamma)
                x, y, z = (c + 0.12 * self.rng.standard_normal() for c in self.accel_of(ab, ag))
                mag = math.sqrt(x * x + y * y + z * z)
                msgs.append((t0 + t + 0.03, phone_id, "accel",
                             {"x": round(x, 3), "y": round(y, 3), "z": round(z, 3), "mag": round(mag, 3)}))
        return msgs


# ------------------------------------------------------------------------------------ frames
@dataclass
class Frame:
    t: float
    beta: float
    gamma: float
    dbeta: float
    dgamma: float
    accel: tuple | None = None        # (x, y, z) nearest in time, or None when none is fresh
    accel_age: float | None = None

    def vector(self):
        return [self.beta, self.gamma, self.dbeta, self.dgamma]


class Tracker:
    """Per-phone state: the previous orient reading and a short accel history."""

    def __init__(self):
        self.prev = None
        self.accel = deque(maxlen=64)

    def feed(self, t, topic, p):
        if topic == "accel":
            try:
                self.accel.append((t, float(p["x"]), float(p["y"]), float(p["z"])))
            except (KeyError, TypeError, ValueError):
                pass
            return None
        if topic != "orient":
            return None
        try:
            beta, gamma = float(p["beta"]), float(p["gamma"])
        except (KeyError, TypeError, ValueError):
            return None
        if self.prev is None:
            dbeta = dgamma = 0.0
        else:
            dt = max(t - self.prev[0], 0.05)
            dbeta = angdiff(beta, self.prev[1]) / dt
            dgamma = angdiff(gamma, self.prev[2]) / dt
        self.prev = (t, beta, gamma)
        acc = age = None
        if self.accel:
            ta, x, y, z = min(self.accel, key=lambda a: abs(a[0] - t))
            if abs(ta - t) <= ACCEL_MAX_AGE:
                acc, age = (x, y, z), abs(ta - t)
        return Frame(t, beta, gamma, dbeta, dgamma, acc, age)


def frames_from_messages(msgs, phone_id=None):
    """Replay messages (sorted by time) into frames for one phone (the busiest id if not given)."""
    msgs = sorted(msgs, key=lambda m: m[0])
    if phone_id is None:
        counts = {}
        for _, pid, topic, _ in msgs:
            if topic == "orient":
                counts[pid] = counts.get(pid, 0) + 1
        if not counts:
            return []
        phone_id = max(counts, key=counts.get)
    tr, frames = Tracker(), []
    for t, pid, topic, p in msgs:
        if pid != phone_id:
            continue
        f = tr.feed(t, topic, p)
        if f is not None:
            frames.append(f)
    return frames


# ------------------------------------------------------------------------------------ fusion (AML.M0009)
SIGNS = [(sx, sy, sz) for sx in (1, -1) for sy in (1, -1) for sz in (1, -1)]


def accel_attitude(x, y, z, signs):
    """Tilt implied by the gravity vector. signs = the axis convention calibrated from the
    baseline (Android and iOS browsers disagree on accel signs)."""
    sx, sy, sz = signs
    x, y, z = x * sx, y * sy, z * sz
    return math.degrees(math.atan2(y, math.hypot(x, z))), math.degrees(math.atan2(-x, z))


def fusion_residual(frame, signs):
    """How far the reported tilt is from the tilt gravity implies (degrees), or None if no
    fresh accelerometer reading exists. γ is weighted by cos β (ill-conditioned near upright)."""
    if frame.accel is None:
        return None
    b, g = accel_attitude(*frame.accel, signs)
    return max(abs(angdiff(frame.beta, b)),
               abs(angdiff(frame.gamma, g)) * abs(math.cos(math.radians(frame.beta))))


def calibrate_fusion(frames):
    """Learn the accel sign convention and a robust residual bound from the baseline.
    Robust (median + MAD), so a minority of poisoned frames cannot widen the bound."""
    usable = [f for f in frames if f.accel is not None]
    if len(usable) < 50:
        return None
    best, best_med = None, 1e9
    for s in SIGNS:
        med = float(np.median([fusion_residual(f, s) for f in usable]))
        if med < best_med:
            best, best_med = s, med
    if best_med > 10.0:
        return None                                    # accel does not track orientation
    res = np.array([fusion_residual(f, best) for f in usable])
    mad = float(np.median(np.abs(res - best_med))) * 1.4826
    return {"signs": list(best), "threshold_deg": round(max(8.0, best_med + 8.0 * mad), 2),
            "median_deg": round(best_med, 2), "frames": len(usable)}


def gate_reason(frame, gate):
    """The engineering envelope in front of the model (AML.M0015) — configured, not learned."""
    lo, hi = gate["beta"]
    if not lo <= frame.beta <= hi:
        return f"gate: tilt β={frame.beta:.1f}° outside [{lo:g},{hi:g}]"
    lo, hi = gate["gamma"]
    if not lo <= frame.gamma <= hi:
        return f"gate: tilt γ={frame.gamma:.1f}° outside [{lo:g},{hi:g}]"
    r = max(abs(frame.dbeta), abs(frame.dgamma))
    if r > gate["max_rate_dps"]:
        return f"gate: tilt rate {r:.0f}°/s > {gate['max_rate_dps']:g}°/s"
    return None


# ------------------------------------------------------------------------------------ the detector
@dataclass
class Verdict:
    verdict: str                      # "OK" | "ALARM"
    reasons: list = field(default_factory=list)
    score: float | None = None        # only ever SHOWN by the vuln profile

    def as_dict(self, profile):
        d = {"verdict": self.verdict, "reasons": self.reasons}
        if profile == "vuln" and self.score is not None:
            d["score"] = round(self.score, 4)
        return d


def _fit(X, seed):
    """A Gaussian-mixture density over the standardized features: it learns what normal tilt looks
    like as a probability density, and a reading whose likelihood falls below the threshold alarms.
    (Isolation Forest was tried first and rejected: its score saturates outside the training range,
    so a blatant -120 deg spoof scored like the most extreme NORMAL reading. See README.)"""
    from sklearn.mixture import GaussianMixture
    X = np.asarray(X, dtype=float)
    mean = X.mean(axis=0)
    std = np.maximum(X.std(axis=0), FEATURE_FLOOR)
    Z = (X - mean) / std
    model = GaussianMixture(n_components=4, covariance_type="full", reg_covar=1e-3,
                            random_state=seed).fit(Z)
    threshold = float(np.quantile(model.score_samples(Z), SCORE_QUANTILE))
    return model, mean, std, threshold


class Detector:
    def __init__(self, model, mean, std, threshold, fusion=None, gate=None, meta=None):
        self.model, self.mean, self.std = model, np.asarray(mean, float), np.asarray(std, float)
        self.threshold = float(threshold)
        self.fusion = fusion
        self.gate = gate or dict(DEFAULT_GATE)
        self.meta = meta or {}
        self.adapt = None              # vuln-only online learning, see enable_adapt()

    # -- scoring
    def scores(self, vecs):
        Z = (np.asarray(vecs, float).reshape(-1, len(FEATURES)) - self.mean) / self.std
        return self.model.score_samples(Z)

    def score(self, vec):
        return float(self.scores([vec])[0])

    def _verdict(self, frame, s, profile):
        reasons = []
        if s < self.threshold:
            reasons.append(f"model: log-likelihood {s:.2f} < threshold {self.threshold:.2f}")
        if profile == "secure":
            if not self.fusion:
                reasons.append("fusion: not calibrated — cannot corroborate (fail closed)")
            else:
                r = fusion_residual(frame, self.fusion["signs"])
                if r is None:
                    reasons.append("fusion: no fresh accelerometer reading — uncorroborated (fail closed)")
                elif r > self.fusion["threshold_deg"]:
                    reasons.append(f"fusion: reported tilt is {r:.1f}° from the tilt gravity implies "
                                   f"(bound {self.fusion['threshold_deg']:g}°)")
        return Verdict("ALARM" if reasons else "OK", reasons, s)

    def evaluate(self, frame, profile="vuln"):
        return self.evaluate_many([frame], profile)[0]

    def evaluate_many(self, frames, profile="vuln"):
        """Verdicts for a sequence of frames. secure: the gate runs first and an out-of-envelope
        reading never reaches the model. vuln + online learning: sequential, refitting as it goes."""
        out = [None] * len(frames)
        todo = []
        for i, f in enumerate(frames):
            g = gate_reason(f, self.gate) if profile == "secure" else None
            if g:
                out[i] = Verdict("ALARM", [g])
            else:
                todo.append(i)
        if profile == "vuln" and self.adapt is not None:
            for i in todo:                              # learn as you go: score, accept, maybe refit
                v = self._verdict(frames[i], self.score(frames[i].vector()), profile)
                if v.verdict == "OK":
                    self._adapt_step(frames[i].vector())
                out[i] = v
            return out
        if todo:
            sc = self.scores([frames[i].vector() for i in todo])
            for i, s in zip(todo, sc):
                out[i] = self._verdict(frames[i], float(s), profile)
        return out

    # -- vuln-only: online re-baselining on accepted readings (the AML.T0031 surface)
    def enable_adapt(self, seed_frames=None, window=3000, every=200, seed=0):
        """vuln-only: keep learning from every reading it ACCEPTS (a rolling window seeded with the
        training data, refit every `every` accepted readings) — the AML.T0031 erosion surface."""
        buf = deque(maxlen=window)
        for f in (seed_frames or [])[-window:]:
            buf.append(f.vector())
        self.adapt = {"buf": buf, "every": every, "n": 0, "seed": seed, "refits": 0}

    def _adapt_step(self, vec):
        a = self.adapt
        a["buf"].append(vec)
        a["n"] += 1
        if a["n"] % a["every"] == 0 and len(a["buf"]) >= 300:
            self.model, self.mean, self.std, self.threshold = _fit(list(a["buf"]), a["seed"])
            a["refits"] += 1


def train(frames, profile="vuln", seed=0, source=None):
    """Fit the detector. secure: refuses without a calibrated fusion channel and drops every
    frame the gate or the fusion check rejects before fitting (AML.M0007 Sanitize Training Data)."""
    if len(frames) < 300:
        raise AidetError(f"only {len(frames)} tilt readings — record at least ~5 minutes")
    fusion = calibrate_fusion(frames)
    dropped = 0
    if profile == "secure":
        if fusion is None:
            raise AidetError("fusion could not be calibrated: share BOTH Motion and Orientation while "
                             "recording, so the accelerometer can corroborate the tilt")
        keep = []
        for f in frames:
            r = fusion_residual(f, fusion["signs"])
            if gate_reason(f, DEFAULT_GATE) is None and r is not None and r <= fusion["threshold_deg"]:
                keep.append(f)
        dropped = len(frames) - len(keep)
        frames = keep
        if len(frames) < 300:
            raise AidetError(f"after sanitizing, only {len(frames)} corroborated readings remain")
    model, mean, std, thr = _fit([f.vector() for f in frames], seed)
    import sklearn
    meta = {"features": list(FEATURES), "trained_frames": len(frames), "dropped_frames": dropped,
            "profile": profile, "seed": seed, "score_quantile": SCORE_QUANTILE,
            "algorithm": "sklearn.mixture.GaussianMixture(n_components=4, covariance_type=full)",
            "sklearn": sklearn.__version__, "numpy": np.__version__,
            "trained_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    if source:
        meta["dataset"] = source
    return Detector(model, mean, std, thr, fusion, dict(DEFAULT_GATE), meta)


# ------------------------------------------------------------------------------------ query oracle
class Oracle:
    """What the detector answers to a direct query (an HTTP /score endpoint in detect.py).

    vuln:   always on; returns the raw score AND the threshold (a perfect optimization oracle).
    secure: off unless explicitly enabled (AML.M0019); returns the verdict only (AML.M0002);
            at most `budget` queries per client per `window_s` (AML.M0004); every query logged."""

    def __init__(self, det, profile, enabled=None, budget=10, window_s=60.0):
        self.det, self.profile = det, profile
        self.enabled = (profile == "vuln") if enabled is None else enabled
        self.budget, self.window_s = budget, window_s
        self.hist = {}
        self.log = []

    def query(self, beta, gamma, dbeta=0.0, dgamma=0.0, client="anon", now=None):
        now = time.time() if now is None else now
        if not self.enabled:
            raise AidetError("query endpoint disabled (secure profile)")
        if self.profile == "secure":
            h = [t for t in self.hist.get(client, []) if now - t < self.window_s]
            if len(h) >= self.budget:
                self.log.append((now, client, "REFUSED"))
                raise AidetError(f"query budget exhausted ({self.budget} per {self.window_s:g} s)")
            h.append(now)
            self.hist[client] = h
        f = Frame(now, float(beta), float(gamma), float(dbeta), float(dgamma))
        if self.profile == "vuln":
            s = self.det.score(f.vector())
            self.log.append((now, client, "score"))
            return {"verdict": "ALARM" if s < self.det.threshold else "OK",
                    "score": round(s, 5), "threshold": round(self.det.threshold, 5)}
        v = self.det.evaluate(f, "secure")
        self.log.append((now, client, v.verdict))
        # model-only verdict: a corroboration failure is not the oracle's business here
        model_alarm = any(r.startswith(("model", "gate")) for r in v.reasons)
        return {"verdict": "ALARM" if model_alarm else "OK"}


# ------------------------------------------------------------------------------------ keys (vendor)
def keygen(vendor_dir=VENDOR_DIR, trust_dir=TRUST_DIR, force=False):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    key_path = os.path.join(vendor_dir, "vendor.key")
    pub_path = os.path.join(trust_dir, "vendor.pub")
    if os.path.exists(key_path) and not force:
        return key_path, pub_path
    os.makedirs(vendor_dir, exist_ok=True)
    os.makedirs(trust_dir, exist_ok=True)
    k = Ed25519PrivateKey.generate()
    with open(key_path, "wb") as f:
        f.write(k.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption()))
    os.chmod(key_path, 0o600)
    with open(pub_path, "wb") as f:
        f.write(k.public_key().public_bytes(serialization.Encoding.PEM,
                                            serialization.PublicFormat.SubjectPublicKeyInfo))
    return key_path, pub_path


def _sign(data, key_path):
    from cryptography.hazmat.primitives import serialization
    with open(key_path, "rb") as f:
        k = serialization.load_pem_private_key(f.read(), password=None)
    return k.sign(data)


def _verify_sig(data, sig, pub_path):
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import serialization
    if not os.path.exists(pub_path):
        raise AidetError(f"no trusted vendor key at {pub_path} (run: python ai-detector/vendor.py keygen)")
    with open(pub_path, "rb") as f:
        pub = serialization.load_pem_public_key(f.read())
    try:
        pub.verify(sig, data)
    except InvalidSignature:
        raise AidetError("manifest signature does NOT verify against the trusted vendor key")


# ------------------------------------------------------------------------------------ dataset provenance (AML.M0025)
def approved_list(trust_dir=TRUST_DIR):
    p = os.path.join(trust_dir, "approved-datasets.txt")
    if not os.path.exists(p):
        return {}
    out = {}
    with open(p) as f:
        for line in f:
            parts = line.split()
            if parts and not line.startswith("#"):
                out[parts[0]] = line.strip()
    return out


def approve_dataset(path, who, note="", trust_dir=TRUST_DIR):
    h = sha256_file(path)
    os.makedirs(trust_dir, exist_ok=True)
    p = os.path.join(trust_dir, "approved-datasets.txt")
    if h not in approved_list(trust_dir):
        with open(p, "a") as f:
            f.write(f"{h}  {os.path.basename(path)}  approved-by={who}  "
                    f"{time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}  {note}\n")
    return h


def require_approved(path, trust_dir=TRUST_DIR):
    h = sha256_file(path)
    if h not in approved_list(trust_dir):
        raise AidetError(f"dataset {os.path.basename(path)} (sha256 {h[:16]}…) is not on the approved list "
                         f"{os.path.join(trust_dir, 'approved-datasets.txt')} — review it, then "
                         f"`python ai-detector/vendor.py approve-dataset {path}`")
    return h


# ------------------------------------------------------------------------------------ artifacts
def _state(det):
    return {"mean": det.mean.tolist(), "std": det.std.tolist(), "threshold": det.threshold,
            "fusion": det.fusion, "gate": det.gate, "meta": det.meta}


def save_pickle(det, path):
    """vuln artifact: one pickle — convenient, and executable on load (CWE-502)."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        pickle.dump({"format": "aidet-pickle-1", "model": det.model, **_state(det)}, f)
    return path


def load_pickle(path):
    """vuln loader: whatever the file says to do, pickle does — before we look at it."""
    with open(path, "rb") as f:
        st = pickle.load(f)                       # CWE-502: deserializing untrusted data
    return Detector(st["model"], st["mean"], st["std"], st["threshold"], st.get("fusion"),
                    st.get("gate"), st.get("meta"))


def save_signed(det, outdir, key_path=None, version="1.0.0", dataset_path=None):
    """secure artifact: model.skops (data, not code) + model.json + a signed manifest that pins
    the SHA-256 of every file and of the training dataset (AML.M0013 / AML.M0014 / AML.M0025)."""
    import skops.io as sio
    key_path = key_path or os.path.join(VENDOR_DIR, "vendor.key")
    if not os.path.exists(key_path):
        raise AidetError(f"no vendor signing key at {key_path} (run: python ai-detector/vendor.py keygen)")
    os.makedirs(outdir, exist_ok=True)
    for stale in os.listdir(outdir):
        os.unlink(os.path.join(outdir, stale))
    sio.dump(det.model, os.path.join(outdir, "model.skops"))
    with open(os.path.join(outdir, "model.json"), "w") as f:
        json.dump(_state(det), f, indent=2)
    manifest = {"name": "cs460-tilt-anomaly", "version": version, "format": "aidet-signed-1",
                "files": {n: sha256_file(os.path.join(outdir, n)) for n in ("model.skops", "model.json")},
                "dataset": det.meta.get("dataset"),
                "signed_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    if dataset_path:
        manifest["dataset"] = {"file": os.path.basename(dataset_path), "sha256": sha256_file(dataset_path)}
    body = json.dumps(manifest, indent=2, sort_keys=True).encode()
    with open(os.path.join(outdir, "manifest.json"), "wb") as f:
        f.write(body)
    with open(os.path.join(outdir, "manifest.sig"), "wb") as f:
        f.write(_sign(body, key_path))
    return outdir


def load_signed(indir, pub_path=None):
    """secure loader. Order matters: signature → hashes → allowlisted, non-executable load."""
    import skops.io as sio
    pub_path = pub_path or os.path.join(TRUST_DIR, "vendor.pub")
    mpath, spath = os.path.join(indir, "manifest.json"), os.path.join(indir, "manifest.sig")
    if not (os.path.exists(mpath) and os.path.exists(spath)):
        raise AidetError("no signed manifest (manifest.json + manifest.sig) — refusing unsigned artifact")
    body = open(mpath, "rb").read()
    _verify_sig(body, open(spath, "rb").read(), pub_path)
    manifest = json.loads(body)
    for name, want in manifest["files"].items():
        if name.endswith((".pkl", ".pickle", ".joblib")):
            raise AidetError(f"{name}: pickle-family artifacts are never loaded by the secure profile")
        p = os.path.join(indir, name)
        if not os.path.exists(p) or sha256_file(p) != want:
            raise AidetError(f"{name}: SHA-256 does not match the signed manifest — artifact was altered")
    mfile = os.path.join(indir, "model.skops")
    untrusted = sio.get_untrusted_types(file=mfile)
    extra = sorted(set(untrusted) - set(SKOPS_TRUSTED))
    if extra:
        raise AidetError(f"model.skops asks to load types outside the allowlist: {extra}")
    model = sio.load(mfile, trusted=SKOPS_TRUSTED)
    st = json.load(open(os.path.join(indir, "model.json")))
    det = Detector(model, st["mean"], st["std"], st["threshold"], st.get("fusion"), st.get("gate"),
                   st.get("meta"))
    det.meta["manifest"] = manifest
    return det


# ------------------------------------------------------------------------------------ model registry
def registry_current(registry):
    return os.path.join(registry, "current")


def publish(registry, src):
    """Copy an artifact (a model.pkl file, or a signed directory) into registry/current/."""
    cur = registry_current(registry)
    if os.path.isdir(cur):
        shutil.rmtree(cur)
    os.makedirs(cur, exist_ok=True)
    if os.path.isdir(src):
        for n in os.listdir(src):
            shutil.copy2(os.path.join(src, n), cur)
    else:
        shutil.copy2(src, os.path.join(cur, "model.pkl"))
    return cur


def load_from_registry(registry, profile, pub_path=None):
    cur = registry_current(registry)
    if profile == "vuln":
        p = os.path.join(cur, "model.pkl")
        if not os.path.exists(p):
            raise AidetError(f"no model.pkl in {cur}")
        return load_pickle(p)                      # no signature, no hash, no type allowlist
    return load_signed(cur, pub_path)
