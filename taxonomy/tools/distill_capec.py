#!/usr/bin/env python3
"""Distill the MITRE CAPEC XML catalog into CAPEC-<id> -> {name,status,abstraction}."""
import sys,json,hashlib,datetime,xml.etree.ElementTree as ET
src,out=sys.argv[1],sys.argv[2]
raw=open(src,"rb").read(); sha=hashlib.sha256(raw).hexdigest()
root=ET.fromstring(raw)
def ln(t): return t.rsplit("}",1)[-1]
version=root.get("Version"); date=root.get("Date")
pats={}
for el in root.iter():
    if ln(el.tag)=="Attack_Pattern":
        i=el.get("ID")
        if i: pats["CAPEC-"+i]={"name":el.get("Name"),"status":el.get("Status"),"abstraction":el.get("Abstraction")}
o={"framework":"capec","title":"MITRE CAPEC","catalog_version":version,"catalog_date":date,
   "source":{"url":"https://capec.mitre.org/data/xml/capec_latest.xml","sha256":sha},
   "distilled_at":datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
   "counts":{"attack_patterns":len(pats)},"attack_patterns":pats}
json.dump(o,open(out,"w"),indent=1,sort_keys=True); print("capec:",version,len(pats))
