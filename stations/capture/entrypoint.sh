#!/usr/bin/env bash
# Capture station — a passive sniffer on the plant network, like a span port. It runs in
# the HOST network namespace so it can see unicast traffic BETWEEN the other containers on
# the Docker bridge (a switched bridge hides that from an ordinary peer container).
set -e
# find the bridge interface for the plant network (named *_plant or br-*)
IFACE="${CAPTURE_IFACE:-}"
if [ -z "$IFACE" ]; then
  IFACE=$(ip -o link show | awk -F': ' '/br-/{print $2}' | head -1)
fi
echo "[capture] sniffing OT protocols on ${IFACE:-any} (Modbus :502, DNP3 :20000, MQTT :1883, phone MQTT-over-WebSocket :8090)"
echo "[capture] live decode below; a pcap is also written to /captures/plant.pcap"
mkdir -p /captures
# write a rotating pcap AND print a live decoded summary
# MQTT :1883 is the controller's subscription to the broker; :8090 carries the phones' MQTT inside
# WebSocket (the Codespace forward terminates TLS outside the container, so on this bridge it is
# readable). Both are plaintext JSON on the wire: anyone on the network can read and forge them.
FILTER="tcp port 502 or tcp port 20000 or tcp port 1883 or tcp port 8090"
tshark -i "${IFACE:-any}" -f "$FILTER" \
       -w /captures/plant.pcap -P 2>/dev/null &
exec tshark -i "${IFACE:-any}" -f "$FILTER" -Y "modbus or mqtt or dnp3" -l \
       -T fields -e frame.number -e ip.src -e ip.dst -e _ws.col.Info 2>/dev/null
