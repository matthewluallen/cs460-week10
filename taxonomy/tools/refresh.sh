#!/usr/bin/env bash
# Refresh vendored taxonomy snapshots from UPSTREAM (the deliberate "update" path).
# Default operation is the committed, pinned snapshot + CI validation; run THIS only when you
# choose to update, then review the version/sha diff and commit. Needs network.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"   # taxonomy/
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
echo "==> ATT&CK for ICS"
curl -sSL "https://raw.githubusercontent.com/mitre-attack/attack-stix-data/master/ics-attack/ics-attack.json" -o "$TMP/ics.json"
python3 "$HERE/tools/distill_attack.py" "$TMP/ics.json" --framework attack-ics --title "MITRE ATT&CK for ICS" \
  --url "https://raw.githubusercontent.com/mitre-attack/attack-stix-data/master/ics-attack/ics-attack.json" \
  --repo mitre-attack/attack-stix-data --path ics-attack/ics-attack.json --out "$HERE/vendor/attack-ics.json"
echo "==> MITRE ATLAS"
curl -sSL "https://raw.githubusercontent.com/mitre-atlas/atlas-navigator-data/main/dist/stix-atlas.json" -o "$TMP/atlas.json"
python3 "$HERE/tools/distill_attack.py" "$TMP/atlas.json" --framework atlas --title "MITRE ATLAS" \
  --url "https://raw.githubusercontent.com/mitre-atlas/atlas-navigator-data/main/dist/stix-atlas.json" \
  --repo mitre-atlas/atlas-navigator-data --path dist/stix-atlas.json --out "$HERE/vendor/atlas.json"
echo "==> ATT&CK Enterprise"
curl -sSL "https://raw.githubusercontent.com/mitre-attack/attack-stix-data/master/enterprise-attack/enterprise-attack.json" -o "$TMP/ent.json"
python3 "$HERE/tools/distill_attack.py" "$TMP/ent.json" --framework attack-enterprise --title "MITRE ATT&CK (Enterprise)" \
  --url "https://raw.githubusercontent.com/mitre-attack/attack-stix-data/master/enterprise-attack/enterprise-attack.json" \
  --repo mitre-attack/attack-stix-data --path enterprise-attack/enterprise-attack.json --out "$HERE/vendor/attack-enterprise.json"
echo "==> MITRE CAPEC"
curl -sSL "https://capec.mitre.org/data/xml/capec_latest.xml" -o "$TMP/capec.xml"
python3 "$HERE/tools/distill_capec.py" "$TMP/capec.xml" "$HERE/vendor/capec.json"
echo "==> MITRE D3FEND"
curl -sSL "https://d3fend.mitre.org/ontologies/d3fend.json" -o "$TMP/d3fend.json"
python3 "$HERE/tools/distill_d3fend.py" "$TMP/d3fend.json" "$HERE/vendor/d3fend.json"
echo "==> NIST CSF 2.0"
pip install -q --break-system-packages openpyxl 2>/dev/null || pip install -q openpyxl 2>/dev/null || true
curl -sSL "https://csrc.nist.gov/extensions/nudp/services/json/csf/download?olirids=all" -o "$TMP/csf.xlsx"
python3 "$HERE/tools/distill_csf.py" "$TMP/csf.xlsx" "$HERE/vendor/nist-csf-2.0.json"
echo "==> CWE"
curl -sSL "https://cwe.mitre.org/data/xml/cwec_latest.xml.zip" -o "$TMP/cwe.zip"
XML="$(unzip -Z1 "$TMP/cwe.zip" | head -1)"
python3 "$HERE/tools/distill_cwe.py" "$TMP/cwe.zip" "$XML" "$HERE/vendor/cwe.json"
echo "Refreshed. Review 'git diff taxonomy/vendor/', run taxonomy/tools/validate.py, then commit."
