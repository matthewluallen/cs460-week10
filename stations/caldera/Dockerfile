# CALDERA C2 station for the CS460 stations lab (pinned to a release).
#
# Builds MITRE CALDERA with just the plugins this lab needs (sandcat agent + stockpile
# abilities, plus the magma UI and compass/debrief for viewing operations). The course's
# custom MQTT phone-sensor abilities are NOT baked in — they are bind-mounted at /cs460 and
# merged into stockpile at startup (see entrypoint.sh), so editing them needs no rebuild.
#
# The linux Sandcat agent is compiled on demand by the server at /file/download (go is
# installed), so the engineer station's dropper can fetch it. First build is slow (the magma
# UI is a Vue build); set --build-arg WITH_UI=false for a fast headless server (the scripted
# scenario in run-scenario.py needs no UI).
# syntax=docker/dockerfile:1
ARG CALDERA_REF=5.3.0

FROM node:23-bookworm AS ui
ARG CALDERA_REF
ARG WITH_UI=true
WORKDIR /src
RUN if [ "$WITH_UI" = "true" ]; then \
      git clone --depth 1 --branch ${CALDERA_REF} https://github.com/mitre/caldera . && \
      git submodule update --init --depth 1 plugins/magma && \
      (cd plugins/magma && npm install && npm run build); \
    else mkdir -p /src/plugins/magma/dist; fi

FROM debian:bookworm-slim AS runtime
ARG CALDERA_REF
ENV DEBIAN_FRONTEND=noninteractive PIP_BREAK_SYSTEM_PACKAGES=1
RUN apt-get update && apt-get install -y --no-install-recommends \
      git curl ca-certificates python3 python3-pip python3-dev golang-go gcc && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /usr/src/app
RUN git clone --depth 1 --branch ${CALDERA_REF} https://github.com/mitre/caldera . && \
    git submodule update --init --depth 1 \
      plugins/sandcat plugins/stockpile plugins/manx plugins/compass plugins/debrief plugins/magma && \
    find plugins -mindepth 1 -maxdepth 1 -type d -empty -delete
    # manx is required even when not enabled (core's TCP contact imports it); the find drops the
    # empty placeholder dirs of the submodules we did NOT init, which the plugin scanner would choke on.
RUN pip3 install --no-cache-dir -r requirements.txt
# gocat deps so the server can compile the linux agent on demand (no pre-staged binaries,
# so no mingw/darwin toolchains needed).
RUN cd plugins/sandcat/gocat && go mod tidy && go mod download
COPY --from=ui /src/plugins/magma/dist /usr/src/app/plugins/magma/dist
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
COPY run-scenario.py /usr/src/app/run-scenario.py
RUN chmod +x /usr/local/bin/entrypoint.sh
EXPOSE 8888 7010 7011/udp 7012
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
