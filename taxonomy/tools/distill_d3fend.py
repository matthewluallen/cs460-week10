#!/usr/bin/env python3
"""Distill the D3FEND JSON-LD ontology into D3-<id> -> {label}."""
import sys,json,hashlib,datetime
src,out=sys.argv[1],sys.argv[2]
raw=open(src,"rb").read(); sha=hashlib.sha256(raw).hexdigest()
d=json.loads(raw); g=d.get("@graph",[])
def s(v):
    if isinstance(v,dict): return v.get("@value") or v.get("@id")
    if isinstance(v,list): return s(v[0]) if v else None
    return v
tech={}
for n in g:
    did=n.get("d3f:d3fend-id")
    if did:
        did=s(did)
        if did: tech[did]={"label":s(n.get("rdfs:label")),"iri":n.get("@id")}
o={"framework":"d3fend","title":"MITRE D3FEND",
   "source":{"url":"https://d3fend.mitre.org/ontologies/d3fend.json","sha256":sha},
   "distilled_at":datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
   "counts":{"techniques":len(tech)},"techniques":tech}
json.dump(o,open(out,"w"),indent=1,sort_keys=True); print("d3fend techniques:",len(tech))
