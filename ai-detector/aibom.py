#!/usr/bin/env python3
"""ai-detector/aibom.py — an AI Bill of Materials for the detector (CycloneDX 1.6 ML-BOM).

    python ai-detector/aibom.py --artifact ai-detector/out/model-signed \\
        --dataset ai-detector/out/baseline.jsonl --out ai-detector/out/aibom.cdx.json
    python ai-detector/aibom.py --check ai-detector/out/aibom.cdx.json --artifact ai-detector/registry/current

Part 5 showed an SBOM only sees what it can parse. Run syft on this folder and it finds the
Python packages in requirements.txt — and nothing about the MODEL that makes the decision or the
DATA that taught it. The AI-BOM (AML.M0023 Maintain an AI Bill of Materials) records both, with
hashes, plus the model card: what it was trained on, what it takes in, what it can't do.
--check recomputes the hashes of a deployed artifact and reports drift from the AI-BOM.
"""
import argparse
import json
import os
import sys
import time
import uuid
from importlib import metadata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aidet  # noqa: E402

LIBS = ["scikit-learn", "numpy", "scipy", "skops", "cryptography", "paho-mqtt"]


def artifact_files(path):
    """Return (format, {filename: sha256}, meta, dataset) for a pickle file or signed directory."""
    if os.path.isdir(path):
        if os.path.exists(os.path.join(path, "manifest.json")):
            man = json.load(open(os.path.join(path, "manifest.json")))
            st = json.load(open(os.path.join(path, "model.json")))
            files = {n: aidet.sha256_file(os.path.join(path, n)) for n in sorted(os.listdir(path))}
            return "skops+signed-manifest", files, st.get("meta", {}), man.get("dataset"), st, man
        path = os.path.join(path, "model.pkl")
    st = aidet.load_pickle(path).__dict__ if False else None   # never unpickle just to inventory
    return "pickle", {os.path.basename(path): aidet.sha256_file(path)}, {}, None, None, None


def build(artifact, dataset):
    fmt, files, meta, ds, st, man = artifact_files(artifact)
    if dataset:
        dh = aidet.sha256_file(dataset)
        if ds and ds.get("sha256") != dh:
            print(f"WARNING: {dataset} (sha256 {dh[:16]}…) is NOT the dataset the model was trained on "
                  f"({ds.get('sha256', '?')[:16]}…)")
        ds = {"file": os.path.basename(dataset), "sha256": dh}
    model_ref, data_ref, app_ref = "model:cs460-tilt-anomaly", "data:tilt-baseline", "app:cs460-ai-detector"
    libs = []
    for name in LIBS:
        try:
            v = metadata.version(name)
        except metadata.PackageNotFoundError:
            continue
        libs.append({"type": "library", "bom-ref": f"pkg:pypi/{name}@{v}", "name": name, "version": v,
                     "purl": f"pkg:pypi/{name}@{v}"})
    thr = (st or {}).get("threshold")
    fusion = (st or {}).get("fusion") or {}
    model = {
        "type": "machine-learning-model", "bom-ref": model_ref, "name": "cs460-tilt-anomaly",
        "version": (man or {}).get("version", "unversioned"),
        "description": "Flags phone tilt readings (MQTT phone/<id>/orient) that do not look like normal "
                       "operation. Alarm only; never writes the controller.",
        "hashes": [{"alg": "SHA-256", "content": h} for n, h in files.items() if n.startswith("model.")],
        "properties": [{"name": "cs460:artifact-format", "value": fmt},
                       {"name": "cs460:artifact-files", "value": ", ".join(f"{n}={h[:16]}…" for n, h in files.items())},
                       {"name": "cs460:signature", "value": "Ed25519 over manifest.json (manifest.sig)"
                        if man else "none"}],
        "modelCard": {
            "modelParameters": {
                "approach": {"type": "unsupervised"},
                "task": "anomaly-detection",
                "architectureFamily": "Gaussian mixture density model",
                "modelArchitecture": meta.get("algorithm", "sklearn.mixture.GaussianMixture"),
                "datasets": [{"ref": data_ref}],
                "inputs": [{"format": "tilt beta, gamma (deg) and their rates (deg/s), standardized"}],
                "outputs": [{"format": "verdict OK | ALARM (log-likelihood below threshold)"}],
            },
            "quantitativeAnalysis": {"performanceMetrics": [
                {"type": "decision threshold (log-likelihood)", "value": "unknown" if thr is None else f"{thr:.4f}"},
                {"type": "training false-alarm quantile", "value": str(meta.get("score_quantile", "unknown"))},
                {"type": "accelerometer cross-check bound (deg)", "value": str(fusion.get("threshold_deg", "none"))},
            ]},
            "considerations": {
                "useCases": ["Alarm on phone-tilt readings that look implausible, for an operator to check."],
                "technicalLimitations": [
                    "A spoof that stays inside the learned envelope at a normal rate is NOT flagged by the model alone.",
                    "The accelerometer cross-check uses a channel from the SAME phone; one source forging both "
                    "channels is not caught.",
                    "Whatever was on the wire during the baseline recording is 'normal' to the model.",
                ],
            },
        },
    }
    data = {"type": "data", "bom-ref": data_ref, "name": (ds or {}).get("file", "unknown baseline"),
            "hashes": [{"alg": "SHA-256", "content": ds["sha256"]}] if ds else [],
            "data": [{"type": "dataset", "name": "phone tilt baseline recording",
                      "description": "MQTT phone/<id>/orient and phone/<id>/accel messages recorded during "
                                     "normal operation (record.py).",
                      "classification": "operational sensor telemetry"}]}
    bom = {
        "bomFormat": "CycloneDX", "specVersion": "1.6", "serialNumber": f"urn:uuid:{uuid.uuid4()}", "version": 1,
        "metadata": {
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "tools": {"components": [{"type": "application", "name": "cs460 ai-detector/aibom.py", "version": "1.0"}]},
            "component": {"type": "application", "bom-ref": app_ref, "name": "cs460-ai-detector",
                          "description": "E4 anomaly detector on the phone-sensor stream"},
        },
        "components": [model, data] + libs,
        "dependencies": [
            {"ref": app_ref, "dependsOn": [model_ref] + [x["bom-ref"] for x in libs]},
            {"ref": model_ref, "dependsOn": [data_ref]},
            {"ref": data_ref, "dependsOn": []},
        ] + [{"ref": x["bom-ref"], "dependsOn": []} for x in libs],
    }
    return bom


def check(bom_path, artifact):
    bom = json.load(open(bom_path))
    model = next(c for c in bom["components"] if c["type"] == "machine-learning-model")
    want = {h["content"] for h in model.get("hashes", [])}
    _, files, *_ = artifact_files(artifact)
    have = {h for n, h in files.items() if n.startswith("model.")}
    extra = sorted(n for n in files if n.endswith((".pkl", ".pickle", ".joblib")) and files[n] not in want)
    if want == have and not extra:
        print(f"MATCH: the deployed model files are the ones in {os.path.basename(bom_path)}")
        return 0
    print(f"DRIFT: deployed model files differ from the AI-BOM {os.path.basename(bom_path)}")
    for n, h in files.items():
        print(f"  {n:14} {h[:16]}…  {'in AI-BOM' if h in want else 'NOT in AI-BOM'}")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--artifact", required=True, help="model.pkl, a signed directory, or registry/current")
    ap.add_argument("--dataset", help="the baseline JSONL the model was trained on")
    ap.add_argument("--out", default=os.path.join(aidet.HERE, "out", "aibom.cdx.json"))
    ap.add_argument("--check", metavar="AIBOM", help="compare a deployed artifact against this AI-BOM")
    a = ap.parse_args()
    if a.check:
        return check(a.check, a.artifact)
    bom = build(a.artifact, a.dataset)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as f:
        json.dump(bom, f, indent=2)
    n_lib = sum(1 for c in bom["components"] if c["type"] == "library")
    print(f"wrote AI-BOM {a.out}")
    print(f"  CycloneDX {bom['specVersion']}: 1 machine-learning-model + 1 data component + {n_lib} libraries")
    print("  syft/grype (Part 5) list the libraries; only this AI-BOM records the model and its training data")
    return 0


if __name__ == "__main__":
    sys.exit(main())