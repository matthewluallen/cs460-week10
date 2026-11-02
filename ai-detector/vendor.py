#!/usr/bin/env python3
"""ai-detector/vendor.py — the VENDOR's role: signing key, dataset approval, publishing.

    python ai-detector/vendor.py keygen                       # Ed25519 key pair (once)
    python ai-detector/vendor.py approve-dataset FILE --by NAME
    python ai-detector/vendor.py publish --registry ai-detector/registry --artifact ai-detector/out/model-signed
    python ai-detector/vendor.py status

Where things live (outside the repo and outside the registry, on purpose):
  ~/.cs460-ai/vendor/vendor.key            the PRIVATE signing key (0600) — the vendor keeps this
  ~/.cs460-ai/trust/vendor.pub             the public key every secure detector trusts
  ~/.cs460-ai/trust/approved-datasets.txt  SHA-256 of every baseline a human reviewed (AML.M0025)
The attacker in this lab can write to the model registry. They cannot read the vendor key.
(Override the location with AIDET_HOME.)
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aidet  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("--force", action="store_true", help="replace an existing key pair")
    d = sub.add_parser("approve-dataset")
    d.add_argument("file")
    d.add_argument("--by", default=os.environ.get("USER", "student"))
    d.add_argument("--note", default="")
    p = sub.add_parser("publish")
    p.add_argument("--registry", default=os.path.join(aidet.HERE, "registry"))
    p.add_argument("--artifact", required=True, help="model.pkl or a signed artifact directory")
    sub.add_parser("status")
    a = ap.parse_args()

    if a.cmd == "keygen":
        key, pub = aidet.keygen(force=a.force)
        print(f"vendor private key: {key}\ntrusted public key: {pub}")
    elif a.cmd == "approve-dataset":
        h = aidet.approve_dataset(a.file, a.by, a.note)
        print(f"approved {a.file}\n  sha256 {h}\n  (only approve a recording you reviewed: what is in it becomes 'normal')")
    elif a.cmd == "publish":
        cur = aidet.publish(a.registry, a.artifact)
        print(f"published {a.artifact} -> {cur}")
        for n in sorted(os.listdir(cur)):
            print(f"  {n:14} sha256 {aidet.sha256_file(os.path.join(cur, n))[:16]}…")
    else:
        print(f"AIDET_HOME   {aidet.HOME}")
        print(f"vendor key   {os.path.join(aidet.VENDOR_DIR, 'vendor.key')}  "
              f"{'present' if os.path.exists(os.path.join(aidet.VENDOR_DIR, 'vendor.key')) else 'MISSING'}")
        print(f"trusted pub  {os.path.join(aidet.TRUST_DIR, 'vendor.pub')}  "
              f"{'present' if os.path.exists(os.path.join(aidet.TRUST_DIR, 'vendor.pub')) else 'MISSING'}")
        ap_list = aidet.approved_list()
        print(f"approved datasets: {len(ap_list)}")
        for line in ap_list.values():
            print(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
