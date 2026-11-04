#!/usr/bin/env python3
"""Distill a MITRE ATT&CK STIX 2.1 bundle into a slim, pinned, validation-ready index.

Input : a STIX bundle (ics-attack.json / enterprise-attack.json).
Output: {framework,title,stix_collection_version,modified,source{...,sha256},distilled_at,
         techniques{EXTID:{name,status,subtechnique,tactics,revoked_by}},
         mitigations{EXTID:{name}}, tactics{shortname:name}}
Only facts + MITRE identifiers are emitted (openly licensed; attribution in registry)."""
import json, sys, hashlib, datetime, argparse

def extid(o):
    for r in o.get("external_references", []):
        if r.get("source_name", "").startswith("mitre"):
            if r.get("external_id"): return r["external_id"]
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bundle"); ap.add_argument("--framework", required=True)
    ap.add_argument("--title", required=True); ap.add_argument("--url", required=True)
    ap.add_argument("--repo", required=True); ap.add_argument("--path", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    raw = open(a.bundle, "rb").read()
    sha = hashlib.sha256(raw).hexdigest()
    d = json.loads(raw); objs = d.get("objects", [])
    by_stix = {o.get("id"): o for o in objs}
    coll = next((o for o in objs if o.get("type") == "x-mitre-collection"), {})
    # revoked-by relationships: old stix -> new stix
    revoked_by = {}
    for o in objs:
        if o.get("type") == "relationship" and o.get("relationship_type") == "revoked-by":
            revoked_by[o.get("source_ref")] = o.get("target_ref")
    # tactic shortname -> name
    tactics = {}
    for o in objs:
        if o.get("type") == "x-mitre-tactic":
            tactics[o.get("x_mitre_shortname")] = o.get("name")
    techniques = {}
    for o in objs:
        if o.get("type") != "attack-pattern": continue
        eid = extid(o)
        if not eid: continue
        status = "active"
        if o.get("revoked"): status = "revoked"
        elif o.get("x_mitre_deprecated"): status = "deprecated"
        entry = {"name": o.get("name"), "status": status,
                 "subtechnique": bool(o.get("x_mitre_is_subtechnique")),
                 "tactics": [p.get("phase_name") for p in o.get("kill_chain_phases", [])]}
        if status == "revoked":
            tgt = revoked_by.get(o.get("id"))
            entry["revoked_by"] = extid(by_stix.get(tgt, {})) if tgt else None
        techniques[eid] = entry
    mitigations = {}
    for o in objs:
        if o.get("type") == "course-of-action":
            eid = extid(o)
            if eid: mitigations[eid] = {"name": o.get("name")}
    out = {"framework": a.framework, "title": a.title,
           "stix_collection_version": coll.get("x_mitre_version"),
           "modified": coll.get("modified"),
           "source": {"repo": a.repo, "path": a.path, "url": a.url, "sha256": sha},
           "distilled_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "counts": {"techniques": len(techniques),
                      "active": sum(1 for t in techniques.values() if t["status"] == "active"),
                      "revoked": sum(1 for t in techniques.values() if t["status"] == "revoked"),
                      "mitigations": len(mitigations)},
           "tactics": tactics, "techniques": techniques, "mitigations": mitigations}
    json.dump(out, open(a.out, "w"), indent=1, sort_keys=True)
    print("wrote", a.out, "| techniques", out["counts"])
if __name__ == "__main__": main()
