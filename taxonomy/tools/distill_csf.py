#!/usr/bin/env python3
"""Distill NIST CSF 2.0 (CPRT xlsx export) -> functions / categories / subcategories id maps."""
import sys,json,re,hashlib,datetime,openpyxl
src,out=sys.argv[1],sys.argv[2]
sha=hashlib.sha256(open(src,"rb").read()).hexdigest()
wb=openpyxl.load_workbook(src,read_only=True,data_only=True)
ws=wb["CSF 2.0"] if "CSF 2.0" in wb.sheetnames else wb[wb.sheetnames[-1]]
FUN=re.compile(r"^(.*?)\s*\((GV|ID|PR|DE|RS|RC)\):\s*(.*)$",re.S)
CAT=re.compile(r"^(.*?)\s*\(((?:GV|ID|PR|DE|RS|RC)\.[A-Z]{2})\):\s*(.*)$",re.S)
SUB=re.compile(r"^((?:GV|ID|PR|DE|RS|RC)\.[A-Z]{2}-\d{2}):\s*(.*)$",re.S)
functions,categories,subcategories={},{},{}
for row in ws.iter_rows(values_only=True):
    for cell in (row[0] if len(row)>0 else None, row[1] if len(row)>1 else None, row[2] if len(row)>2 else None):
        if not isinstance(cell,str): continue
        c=cell.strip()
        m=SUB.match(c)
        if m: subcategories[m.group(1)]={"text":m.group(2).strip()[:300]}; continue
        m=CAT.match(c)
        if m: categories[m.group(2)]={"name":m.group(1).strip()}; continue
        m=FUN.match(c)
        if m: functions[m.group(2)]={"name":m.group(1).strip()}
o={"framework":"nist-csf-2.0","title":"NIST Cybersecurity Framework 2.0",
   "source":{"url":"https://csrc.nist.gov/extensions/nudp/services/json/csf/download?olirids=all","note":"CPRT xlsx export","sha256":sha},
   "distilled_at":datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
   "counts":{"functions":len(functions),"categories":len(categories),"subcategories":len(subcategories)},
   "functions":functions,"categories":categories,"subcategories":subcategories}
json.dump(o,open(out,"w"),indent=1,sort_keys=True); print("csf:",o["counts"])
