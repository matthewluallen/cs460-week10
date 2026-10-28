#!/usr/bin/env bash
# down.sh — stop and remove the attacker container. Wraps `docker compose ... down`.
#
#   ./attacker/down.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/../tools/teach.sh" 2>/dev/null || true
type say >/dev/null 2>&1 || { step(){ printf '\n== %s ==\n' "$*"; }; say(){ printf '>> %s\n' "$*"; }; run(){ printf '   $ %s\n' "$*"; "$@"; }; observe(){ printf '-> %s\n' "$*"; }; predict(){ printf '?? PREDICT FIRST: %s\n' "$*"; }; finding(){ printf '** FINDING: %s\n' "$*"; }; note(){ printf '   %s\n' "$*"; }; }

step "Stop the attacker host"
say  "Tear down lab-attacker and its labnet bridge. The twin reverts to reaching only"
say  "127.0.0.1-based rogue servers (or none) until you bring the container back up."

run docker compose -f "$HERE/docker-compose.yml" down

observe "lab-attacker stopped. Any 'ntpsync 172.30.0.10 ...' will now fail to reach it."
