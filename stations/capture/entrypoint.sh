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
echo "[capture] sniffing OT protocols on ${IFACE:-any} (Modbus :502, DNP3 :20000)"
echo "[capture] live decode below; a pcap is also written to /captures/plant.pcap"
mkdir -p /captures
# write a rotating pcap AND print a live decoded summary
tshark -i "${IFACE:-any}" -f "tcp port 502 or tcp port 20000" \
       -w /captures/plant.pcap -P 2>/dev/null &
exec tshark -i "${IFACE:-any}" -f "tcp port 502 or tcp port 20000" -l \
       -T fields -e frame.number -e ip.src -e ip.dst -e _ws.col.Info 2>/dev/null
