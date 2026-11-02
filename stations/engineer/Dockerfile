FROM python:3.12-slim
# curl is used by the emulated-delivery dropper to fetch the Sandcat agent from the C2.
RUN apt-get update && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*
COPY lib/ /lib/
COPY engineer/engineer.py /app/engineer.py
COPY engineer/deploy-sandcat.sh /app/deploy-sandcat.sh
RUN chmod +x /app/deploy-sandcat.sh
# default: sit on the network and poll the controller; override the command to write, or run
# /app/deploy-sandcat.sh to land a CALDERA agent (emulated USB/phishing initial access).
CMD ["python3", "/app/engineer.py", "read"]
