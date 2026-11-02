#!/usr/bin/env python3
"""ai-detector/train.py — fit the tilt detector on a recorded baseline.

    python ai-detector/train.py --data ai-detector/out/baseline.jsonl --profile vuln \\
        --out ai-detector/out/model.pkl
    python ai-detector/train.py --data ai-detector/out/baseline.jsonl --profile secure \\
        --out ai-detector/out/model-signed

vuln    trains on whatever file it is given, keeps every frame, writes one pickle.
secure  refuses a dataset that is not on the approved list (AML.M0025), drops every frame the
        engineering gate or the accelerometer cross-check rejects (AML.M0007), and writes a
        signed, hash-pinned, non-pickle artifact (AML.M0013 / AML.M0014). Needs the vendor key:
        python ai-detector/vendor.py keygen
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aidet  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", required=True, help="baseline JSONL (record.py or phonesim.py --out)")
    ap.add_argument("--profile", choices=["vuln", "secure"], default="vuln")
    ap.add_argument("--out", required=True, help="model.pkl (vuln) or a directory (secure)")
    ap.add_argument("--id", help="phone id to train on (default: the busiest id in the file)")
    ap.add_argument("--version", default="1.0.0")
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args()
    try:
        src = {"file": os.path.basename(a.data), "sha256": aidet.sha256_file(a.data)}
        if a.profile == "secure":
            aidet.require_approved(a.data)
        frames = aidet.frames_from_messages(aidet.read_jsonl(a.data), a.id)
        det = aidet.train(frames, a.profile, a.seed, source=src)
        if a.profile == "vuln":
            aidet.save_pickle(det, a.out)
        else:
            aidet.save_signed(det, a.out, version=a.version, dataset_path=a.data)
    except aidet.AidetError as e:
        print(f"REFUSED: {e}")
        return 2
    m = det.meta
    print(f"trained {a.profile} detector on {m['trained_frames']} tilt readings "
          f"(dropped {m['dropped_frames']}) from {src['file']} sha256 {src['sha256'][:16]}…")
    print(f"  model: {m['algorithm']}   threshold (log-likelihood): {det.threshold:.2f}")
    if det.fusion:
        print(f"  fusion: accel axis signs {det.fusion['signs']}, bound {det.fusion['threshold_deg']}° "
              f"(median residual {det.fusion['median_deg']}°)")
    else:
        print("  fusion: NOT calibrated (no usable accelerometer stream)")
    print(f"  -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
