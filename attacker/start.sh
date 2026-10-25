#!/bin/sh
# Run both rogue servers bound to all interfaces, reading their benign/armed state
# from /state so the lab can flip them from the host (docker compose exec ...).
mkdir -p /state
echo "[attacker] host $(hostname -i)  — rogue NTP :1123 (udp+tcp), rogue C2 :9000 (tcp)"
echo "[attacker] arm NTP: echo arm    > /state/ntp_state   (reset: echo benign > ...)"
echo "[attacker] arm C2 : echo ARM    > /state/c2_state"
NTP_HOST=0.0.0.0 NTP_PORT=1123 NTP_STATE=/state/ntp_state python3 /app/ntp_server.py &
C2_HOST=0.0.0.0  C2_PORT=9000  C2_STATE=/state/c2_state   python3 /app/c2_server.py &
wait
