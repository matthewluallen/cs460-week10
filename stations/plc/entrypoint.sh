#!/usr/bin/env bash
# PLC station entrypoint — boots the StampLC twin and the bridge, with the controller's
# Modbus/TCP :502 and HTTP HMI :80 bound to the container's plant-network interface so the
# OTHER stations can reach them. The repo is mounted at /work.
set -e
cd /work
if ! ls qemu/twin/qemu-twin-linux-*/qemu-system-xtensa >/dev/null 2>&1; then
  echo "[plc] twin not present — fetching it (one-time)…"
  ./qemu/fetch-qemu.sh || { echo "[plc] fetch-qemu failed; run it once in the repo before 'docker compose up'"; exit 1; }
fi
export W5_BIND=0.0.0.0          # expose Modbus :502 + HTTP :80 on the plant network
export BRIDGE_BIND=0.0.0.0      # the Studio server must accept the host's port mapping (8080:8080)
export SERIAL_SOCK=/tmp/plc-serial
echo "[plc] booting the twin (${PROFILE:-vuln}) + bridge; Modbus :502, HTTP :80, Studio :8080 on $(hostname -i)"
exec ./qemu/studio.sh "${PROFILE:-vuln}"
