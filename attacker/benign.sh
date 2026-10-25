#!/usr/bin/env bash
# Reset BOTH rogue servers to benign.
set -euo pipefail
cd "$(dirname "$0")"
docker compose -f docker-compose.yml exec -T attacker sh -c 'echo benign > /state/ntp_state; echo benign > /state/c2_state'
echo "attacker reset to benign."
