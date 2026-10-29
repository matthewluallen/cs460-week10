# Phone-sensor MQTT bridge — a single Node process: MQTT broker (TCP :1883 + WS :8090),
# the sensor web page, and the dynamic QR. Builds cleanly in a Codespace (npm, no proxy).
FROM node:20-bookworm-slim
WORKDIR /app
COPY package.json ./
RUN npm install --omit=dev && npm cache clean --force
COPY server.js ./
COPY public ./public
EXPOSE 1883 8090
CMD ["node", "server.js"]
