#!/usr/bin/env bash
# CALDERA station entrypoint: generate a trimmed config, merge the course's custom MQTT
# phone-sensor abilities into stockpile, then start the server.
set -e
APP=/usr/src/app
cd "$APP"

# Build conf/local.yml from the shipped default (local.yml fully replaces default.yml), keeping
# the stock api keys (api_key_red=ADMIN123) but binding to all interfaces and trimming plugins.
PLUGINS="${CALDERA_PLUGINS:-sandcat,stockpile,magma,compass,debrief}"
python3 - "$PLUGINS" <<'PY'
import sys, yaml
d = yaml.safe_load(open('conf/default.yml'))
d['plugins'] = sys.argv[1].split(',')
d['host'] = '0.0.0.0'
d['port'] = 8888
yaml.safe_dump(d, open('conf/local.yml', 'w'))
PY

# Merge the course content (bind-mounted read-only at /cs460) into stockpile's data dirs.
SRC=/cs460/plugins/mqtt_phone/data
if [ -d "$SRC" ]; then
  cp -r "$SRC"/abilities/*   plugins/stockpile/data/abilities/   2>/dev/null || true
  cp    "$SRC"/sources/*.yml plugins/stockpile/data/sources/     2>/dev/null || true
  cp    "$SRC"/adversaries/*.yml plugins/stockpile/data/adversaries/ 2>/dev/null || true
  cp    "$SRC"/payloads/*    plugins/stockpile/data/payloads/     2>/dev/null || true
  echo "[caldera] merged CS460 MQTT phone-sensor abilities into stockpile"
else
  echo "[caldera] note: /cs460 not mounted — the custom MQTT abilities will be absent"
fi

# The core UI static route is registered unconditionally, so this path must exist even when the
# server runs headless (WITH_UI=false). Idempotent: a real built UI already has it.
mkdir -p plugins/magma/dist/assets

echo "[caldera] starting server on :8888 (api_key_red=ADMIN123) plugins=$PLUGINS"
exec python3 server.py --log INFO "$@"
