#!/usr/bin/env python3
"""Validate taxonomy crosswalks against the registry + pinned vendored snapshots.

Integrity rules (fail the build on any ERROR):
  * every ref.framework must be declared in registry.json
  * vendored framework (status=vendored): ref.id must match the id_pattern AND exist in the
    pinned snapshot AND be ACTIVE. A revoked id FAILS and names its replacement; a deprecated
    id FAILS. (This is the drift the D3FEND 1.6.0 mappings hit; here CI catches it.)
  * vendored framework pending a snapshot (status=pending-vendor): id is pattern-checked and
    reported as UNVERIFIED (warning) — cannot confirm existence yet.
  * reference standard (IEC/ISO/NIST-doc): no id lookup; the registry's citation_fields must be
    present. Nothing from the standard's text is stored.
  * automation blocks: %address checked against IEC 61131-3 pattern; -designation and class
    checked against IEC 81346-2 pattern / class list.
Usage: validate.py [--root taxonomy] [crosswalk.json ...]   (default: all crosswalks/*.json)
"""
import json, re, sys, os, glob, argparse

def load(p): return json.load(open(p, encoding="utf-8"))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("crosswalks", nargs="*")
    a = ap.parse_args()
    root = a.root
    reg = load(os.path.join(root, "registry.json"))["frameworks"]
    # load vendored snapshots
    snap = {}
    for k, f in reg.items():
        if f.get("kind") == "vendored" and f.get("status") == "vendored":
            s = load(os.path.join(root, f["snapshot"]))
            ids = {}
            for tid, t in s.get("techniques", {}).items(): ids[tid] = t
            for mid, m in s.get("mitigations", {}).items(): ids[mid] = {"status": "active", "name": m.get("name")}
            for tid, t in s.get("weaknesses", {}).items(): ids[tid] = {"status": (t.get("status") or "active"), "name": t.get("name")}
            snap[k] = ids
    errors, warns, checked = [], [], 0
    files = a.crosswalks or sorted(glob.glob(os.path.join(root, "crosswalks", "*.json")))
    for cf in files:
        cw = load(cf); base = os.path.basename(cf)
        if cw.get("schema_version") != "tbd.taxonomy-association/v1":
            errors.append(f"{base}: bad or missing schema_version"); continue
        for obj in cw.get("objects", []):
            oid = obj.get("id", "?")
            au = obj.get("automation") or {}
            addr = au.get("iec_61131_3_address")
            if addr and not re.match(reg["iec-61131-3"]["address_pattern"], addr):
                errors.append(f"{base} {oid}: '{addr}' is not a valid IEC 61131-3 address")
            desig = au.get("iec_81346_2_designation")
            if desig and not re.match(reg["iec-81346-2"]["designation_pattern"], desig):
                errors.append(f"{base} {oid}: '{desig}' is not a valid IEC 81346-2 designation")
            cls = au.get("iec_81346_2_class")
            if cls and cls not in reg["iec-81346-2"]["class_values"]:
                errors.append(f"{base} {oid}: IEC 81346-2 class '{cls}' not in the standard's class list")
            for r in obj.get("refs", []):
                checked += 1
                fw = r.get("framework")
                if fw not in reg:
                    errors.append(f"{base} {oid}: unknown framework '{fw}'"); continue
                F = reg[fw]
                if F["kind"] == "reference":
                    for field in F.get("citation_fields", []):
                        if not r.get(field):
                            errors.append(f"{base} {oid}: {fw} citation missing required field '{field}'")
                    continue
                rid = r.get("id")
                if not rid:
                    errors.append(f"{base} {oid}: {fw} ref has no id"); continue
                if not re.match(F["id_pattern"], rid):
                    errors.append(f"{base} {oid}: {fw} id '{rid}' violates id_pattern {F['id_pattern']}"); continue
                if F.get("status") == "pending-vendor":
                    warns.append(f"{base} {oid}: {fw} '{rid}' UNVERIFIED (snapshot pending)"); continue
                entry = snap.get(fw, {}).get(rid)
                if entry is None:
                    errors.append(f"{base} {oid}: {fw} '{rid}' not found in pinned snapshot")
                elif entry.get("status") == "revoked":
                    rb = entry.get("revoked_by")
                    errors.append(f"{base} {oid}: {fw} '{rid}' is REVOKED — use {rb or 'its replacement'}")
                elif entry.get("status") == "deprecated":
                    errors.append(f"{base} {oid}: {fw} '{rid}' is DEPRECATED")
    print(f"taxonomy-validate: {len(files)} crosswalk(s), {checked} refs checked")
    for w in warns: print("  WARN ", w)
    for e in errors: print("  ERROR", e)
    if errors:
        print(f"FAILED: {len(errors)} error(s)"); sys.exit(1)
    print(f"OK ({len(warns)} warning(s))")
if __name__ == "__main__": main()
