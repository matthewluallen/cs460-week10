FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      tshark iproute2 && rm -rf /var/lib/apt/lists/*
COPY capture/entrypoint.sh /usr/local/bin/capture.sh
RUN chmod +x /usr/local/bin/capture.sh
ENTRYPOINT ["/usr/local/bin/capture.sh"]
