# PLC station: the runtime the QEMU StampLC twin needs. The repo (with the fetched twin)
# is mounted at /work at run time, so this image is just the shared libraries + python.
# Standard base + apt — builds cleanly in a GitHub Codespace (no proxy, no custom CA).
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      libglib2.0-0 libpixman-1-0 libslirp0 libgcrypt20 libcap2-bin \
      python3 bash netcat-openbsd iproute2 curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /work
COPY entrypoint.sh /usr/local/bin/plc-entrypoint.sh
RUN chmod +x /usr/local/bin/plc-entrypoint.sh
ENTRYPOINT ["/usr/local/bin/plc-entrypoint.sh"]
