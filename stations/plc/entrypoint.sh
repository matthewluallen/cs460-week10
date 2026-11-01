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
export BRIDGE_BIND=0.0.0.0      # the Studio server must accept the host's port mapping (8088:8088)
export BRIDGE_PORT=8088         # Studio port; the firmware's own web HMI keeps :8080 on the plant network
export SERIAL_SOCK=/tmp/plc-serial
echo "[plc] booting the twin (${PROFILE:-vuln}) + bridge; Modbus :502, HTTP :80, web HMI :8080, Studio :8088 on $(hostname -i)"
# Once the firmware finishes booting, point the controller at the phone broker (mqtt station)
# so phone sensors arrive as NETWORK points. Firmware without the `mqtt` command just prints
# "? type 'help'".
MQTT_BROKER="${MQTT_BROKER:-10.20.30.40}"
( # Wait for the twin to finish booting BEFORE sending it anything. Right after boot the
  # firmware holds a ~5 s "Press ENTER" Wi-Fi-config window; a stray newline (e.g. the old
  # `status` probe) sent into that window opens the config menu instead of reaching plc>. So
  # read the console passively — sending nothing — until the boot banner's "Startup complete"
  # scrolls past (mirrors how assessment/verify-lab.py waits for "Startup complete" before it
  # drives the board). `nc -d` never reads stdin, so nothing is injected during the window.
  for _ in $(seq 1 90); do
    [ -n "$(timeout 120 nc -d 127.0.0.1 8081 2>/dev/null | grep -m1 'Startup complete')" ] && break
    sleep 2
  done
  echo "[plc] mqtt connect $MQTT_BROKER"
  (printf 'mqtt connect %s\n' "$MQTT_BROKER"; sleep 3) | timeout 6 nc 127.0.0.1 8081 2>/dev/null | tr -d '\r' | grep -m1 -i 'mqtt' || true
) &
exec ./qemu/studio.sh "${PROFILE:-vuln}"
