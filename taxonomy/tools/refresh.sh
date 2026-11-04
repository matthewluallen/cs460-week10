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
echo "==> CWE"
curl -sSL "https://cwe.mitre.org/data/xml/cwec_latest.xml.zip" -o "$TMP/cwe.zip"
XML="$(unzip -Z1 "$TMP/cwe.zip" | head -1)"
python3 "$HERE/tools/distill_cwe.py" "$TMP/cwe.zip" "$XML" "$HERE/vendor/cwe.json"
echo "Refreshed. Review 'git diff taxonomy/vendor/', run taxonomy/tools/validate.py, then commit."
