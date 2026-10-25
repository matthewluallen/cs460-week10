#!/usr/bin/env bash
# One-command end-to-end validation of the containerized attacker INSIDE a Codespace
# (or any docker-in-docker dev container). Proves the realistic topology:
#   rogue NTP server -> its own container at 172.30.0.10  ->  the StamPLC twin
#   reaches it as a distinct host, benign time is fine, armed time wraps past 2038.
#
#   bash attacker/codespace-e2e.sh
#
# Steps: fetch the twin (once), build+start the attacker container, run the twin and
# drive it (attacker/codespace-validate.py), then tear the container down.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT"

echo "== 1/4  fetch the QEMU twin (idempotent) =="
bash qemu/fetch-qemu.sh

echo "== 2/4  build + start the attacker container (172.30.0.10) =="
docker compose -f attacker/docker-compose.yml up -d --build
# make sure it starts benign
bash attacker/benign.sh >/dev/null 2>&1 || true
docker ps --filter name=lab-attacker --format '   {{.Names}}  {{.Status}}  {{.Networks}}'

echo "== 3/4  run the twin and drive the NTP AiTM end-to-end =="
set +e
python3 attacker/codespace-validate.py
rc=$?
set -e

echo "== 4/4  tear down the attacker container =="
docker compose -f attacker/docker-compose.yml down >/dev/null 2>&1 || true

echo
if [ $rc -eq 0 ]; then
  echo "RESULT: PASS — the twin detonates off the containerized attacker at 172.30.0.10."
else
  echo "RESULT: FAIL (rc=$rc) — see the per-check output above."
fi
exit $rc
