#!/usr/bin/env bash
# Arm BOTH rogue servers in the attacker container (Parts 7 & 8).
set -euo pipefail
cd "$(dirname "$0")"
docker compose -f docker-compose.yml exec -T attacker sh -c 'echo arm > /state/ntp_state; echo ARM > /state/c2_state'
echo "attacker ARMED: NTP serves a post-2038 time; C2 serves the kill-switch trigger."
