# PLC station: the runtime the QEMU StampLC twin needs. The repo (with the fetched twin)
# is mounted at /work at run time, so this image is just the shared libraries + python.
# Ubuntu 24.04 to match the prebuilt twin's toolchain (glibc >= 2.38) — the same base the
# devcontainer uses, so the exact libraries fetch-qemu.sh expects are present. Builds
# cleanly in a GitHub Codespace (no proxy, no custom CA).
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
      libglib2.0-0t64 libpixman-1-0 libslirp0 libgcrypt20 libcap2-bin libpng16-16 \
      python3 bash netcat-openbsd iproute2 curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /work
COPY entrypoint.sh /usr/local/bin/plc-entrypoint.sh
RUN chmod +x /usr/local/bin/plc-entrypoint.sh
ENTRYPOINT ["/usr/local/bin/plc-entrypoint.sh"]
