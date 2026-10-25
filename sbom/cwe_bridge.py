#!/usr/bin/env python3
# cwe_bridge.py — connect the SBOM to the analyzer through the CWE.
#
#   analyzer  <->  SBOM  <->  CWE  <->  CVE
#
# The SBOM (syft->grype) finds a vulnerable *library* (a CVE), and NVD maps that CVE
# to a *CWE* weakness class. But a library CVE is only an *outcome* if the firmware's
# *code* reaches that weak path — "vulnerable libraries vs. the vulnerable code that
# uses them." This tool pivots on the CWE: it joins the scanner's CVEs to the places
# the SAME weakness class lives as *vulnerable code* in the StampLC firmware, and to
# the *analyzer evidence* that shows the outcome at runtime. CWEs with no reachable
# code / no runtime signal are flagged library-only.
#
#   grype dir:sbom/vuln-sample -o json > grype.json
#   python3 sbom/cwe_bridge.py grype.json
import json, sys, re, argparse
from collections import defaultdict

# CWE class -> where it lives as VULNERABLE CODE in the firmware, and the ANALYZER
# evidence that reveals its OUTCOME. (The firmware's own `cwe` command lists 798,
# 319, 306, 494, 78, 489, 330, 22; these map library weakness classes onto it.)
OBSERVABLE = {
 "CWE-319":{"name":"Cleartext transmission",
   "code":"Modbus/TCP + MQTT sent with no TLS (modbus_tcp_server.cpp, mqtt_sparkplug.cpp)",
   "evidence":"capture :502/:1883 — coil values and the write are readable in plaintext"},
 "CWE-306":{"name":"Missing authentication for critical function",
   "code":"the coil-write and /api/force paths have no auth check (modbus_tcp_server.cpp, web_hmi.cpp)",
   "evidence":"an unauthenticated write is accepted with no credential: a Modbus coil write is logged (but the scan reasserts outputs), and an HMI /api/force?tag=relay0 forces the relay — the I2C analyzer then shows the 0x59 write"},
 "CWE-295":{"name":"Improper certificate/transport validation",
   "code":"no TLS termination on the northbound links (same transport as CWE-319)",
   "evidence":"no TLS handshake in the capture; the session is readable end to end"},
 "CWE-200":{"name":"Exposure of sensitive information",
   "code":"/api/points returns full device + weakness state unauthenticated",
   "evidence":"GET /api/points dumps every point, mode and force in one response"},
 "CWE-522":{"name":"Insufficiently protected credentials",
   "code":"service password derived from the MAC (plc_auth.cpp; pairs with CWE-798)",
   "evidence":"the derived credential is compared/used over the cleartext channel"},
 "CWE-20": {"name":"Improper input validation",
   "code":"/api bodies parsed by ArduinoJson with no shape/bounds checks",
   "evidence":"a crafted JSON body changes behavior / faults — visible on the console + QMP state"},
 "CWE-502":{"name":"Deserialization of untrusted data",
   "code":"program/config loaded from a reachable channel (program_loader.cpp, TBD-IR)",
   "evidence":"a crafted program blob changes the running logic — the I2C analyzer shows new relay behavior"},
 "CWE-74": {"name":"Injection",
   "code":"diagnostic ping builds a command from input (CWE-78)",
   "evidence":"an injected payload runs; the effect lands in the source-attributed audit log"},
 "CWE-330":{"name":"Use of insufficiently random values",
   "code":"session token from a weak/predictable generator",
   "evidence":"captured tokens across sessions are predictable"},
}

def cwes_for(match):
    out=set()
    for s in re.findall(r'CWE-\d+', json.dumps(match)): out.add(s)
    return out

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("grype_json")
    ap.add_argument("--all",action="store_true",help="also list library-only CWEs")
    a=ap.parse_args()
    d=json.load(open(a.grype_json))
    bycwe=defaultdict(lambda:defaultdict(set))   # cwe -> cve -> {libs}
    for m in d.get("matches",[]):
        art=m["artifact"]; cve=m["vulnerability"]["id"]; lib=f"{art['name']}=={art['version']}"
        for c in cwes_for(m):
            bycwe[c][cve].add(lib)
    print("analyzer  <->  SBOM  <->  CWE  <->  CVE\n")
    obs=[c for c in bycwe if c in OBSERVABLE]
    other=[c for c in bycwe if c not in OBSERVABLE]
    print(f"== Observable in the twin ({len(obs)} CWE classes the analyzer can demonstrate) ==\n")
    for c in sorted(obs):
        o=OBSERVABLE[c]; cves=sorted(bycwe[c])
        libs=sorted({l for v in bycwe[c].values() for l in v})
        print(f"{c} — {o['name']}")
        print(f"   SBOM finds it in : {', '.join(libs)}")
        print(f"   via CVEs         : {', '.join(cves)}")
        print(f"   vulnerable code  : {o['code']}")
        print(f"   analyzer evidence: {o['evidence']}\n")
    if a.all and other:
        print(f"== Library-only in this system ({len(other)} CWE classes — present in a dependency, "
              f"but no reachable firmware code / no runtime signal here) ==")
        for c in sorted(other):
            cves=sorted(bycwe[c]); libs=sorted({l for v in bycwe[c].values() for l in v})
            print(f"   {c}: {', '.join(libs)}  ({len(cves)} CVE(s))")
    print("\nKey idea: the SBOM proves a weakness *class* is present in a library; the "
          "analyzer proves the *outcome* when the firmware's code reaches it. The CWE is the join.")

if __name__=="__main__": main()
