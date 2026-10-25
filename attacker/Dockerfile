# The lab "attacker host" — a container running the rogue NTP time server (Part 8)
# and the rogue C2 / update server (Part 7). It lives on its own Docker network so
# the StamPLC twin sees the attacker as a DISTINCT host (172.30.0.10), not localhost.
# Build context is the repo root (to copy the rogue servers that also ship in the lab).
FROM python:3.12-slim
WORKDIR /app
COPY ntp-aitm/ntp_server.py    /app/ntp_server.py
COPY supply-chain/c2_server.py /app/c2_server.py
COPY attacker/start.sh         /start.sh
RUN chmod +x /start.sh
# rogue NTP on 1123 (udp+tcp), rogue C2 / update server on 9000 (tcp)
EXPOSE 1123/udp 1123/tcp 9000/tcp
CMD ["/start.sh"]
