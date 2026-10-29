# HMI station, built in Node-RED — students see the HMI *wiring* (the flow) and the
# *UI design* (the dashboard), not just a finished page. node-red-contrib-modbus polls the
# PLC over Modbus/TCP; node-red-dashboard renders the gauges. Flow editor on :1880, the
# operator dashboard on :1880/ui .
FROM nodered/node-red:4.0.9

# Modbus I/O nodes + the dashboard UI nodes, installed into the Node-RED app.
RUN npm install --no-audit --no-fund --save \
      node-red-contrib-modbus@5.43.0 \
      node-red-dashboard@3.6.5

# Pre-load the Plant HMI flow (Modbus read -> decode -> gauges + relay LEDs).
COPY flows.json /data/flows.json
COPY settings.js /data/settings.js

EXPOSE 1880
