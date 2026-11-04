#!/usr/bin/env python3
"""Distill the MITRE CWE catalog XML into a slim id->{name,abstraction,status} index."""
import sys, json, hashlib, datetime, zipfile, xml.etree.ElementTree as ET
zip_path, xml_name, out = sys.argv[1], sys.argv[2], sys.argv[3]
raw = open(zip_path, "rb").read(); sha = hashlib.sha256(raw).hexdigest()
with zipfile.ZipFile(zip_path) as z:
    data = z.read(xml_name)
root = ET.fromstring(data)
def ln(tag): return tag.rsplit("}", 1)[-1]
version = root.get("Version"); date = root.get("Date")
weaknesses = {}
for el in root.iter():
    if ln(el.tag) == "Weakness":
        wid = el.get("ID")
        if wid:
            weaknesses["CWE-" + wid] = {"name": el.get("Name"),
                                        "abstraction": el.get("Abstraction"),
                                        "status": el.get("Status")}
out_obj = {"framework": "cwe", "title": "MITRE Common Weakness Enumeration",
           "catalog_version": version, "catalog_date": date,
           "source": {"url": "https://cwe.mitre.org/data/xml/cwec_latest.xml.zip", "sha256": sha},
           "distilled_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "counts": {"weaknesses": len(weaknesses)}, "weaknesses": weaknesses}
json.dump(out_obj, open(out, "w"), indent=1, sort_keys=True)
print("wrote", out, "| version", version, "| weaknesses", len(weaknesses))
