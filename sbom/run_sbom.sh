#!/usr/bin/env bash
# Part 2 helper — generate two SBOM formats and scan for CVEs.
# Run from anywhere:  bash sbom/run_sbom.sh
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out

echo "============================================================"
echo " SBOM of the sample component ( sbom/vuln-sample/ )"
echo "============================================================"

echo
echo "== 1. SPDX format (syft) =="
syft dir:vuln-sample -o spdx-json=out/sbom.spdx.json -q
echo "   wrote out/sbom.spdx.json"

echo
echo "== 2. CycloneDX format (syft) =="
syft dir:vuln-sample -o cyclonedx-json=out/sbom.cdx.json -q
echo "   wrote out/sbom.cdx.json"

echo
echo "== Component counts (note they may differ, and the fields differ) =="
echo "   SPDX packages:    $(python3 -c 'import json;print(len(json.load(open("out/sbom.spdx.json")).get("packages",[])))')"
echo "   CycloneDX comps:  $(python3 -c 'import json;print(len(json.load(open("out/sbom.cdx.json")).get("components",[])))')"

echo
echo "== 3. Vulnerabilities (grype, reading your SBOM) =="
grype sbom:out/sbom.spdx.json -o table | tee out/grype.txt

echo
echo "== 4. Connect to the analyzer:  analyzer <-> SBOM <-> CWE <-> CVE =="
grype sbom:out/sbom.spdx.json -o json > out/grype.json 2>/dev/null
python3 cwe_bridge.py out/grype.json --all | tee out/cwe-bridge.txt

echo
echo "Done. Compare out/sbom.spdx.json vs out/sbom.cdx.json, and read out/grype.txt."
echo "Pick your top 5 CVEs for the report table, then look ONE up in the NVD."
echo "Then read out/cwe-bridge.txt: which weakness *classes* the scanner found also"
echo "have vulnerable *code* in the firmware, and how the analyzer shows the outcome."
