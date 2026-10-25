// eth_http.h - minimal HTTP server on the W5500's own TCP stack (port 80),
// so the browser HMI and the StamPLC editor's IP path work over Ethernet.
// Serviced from the core-0 comms task alongside Modbus. Guarded by ENABLE_ETHERNET.
#pragma once
#include <Arduino.h>
void eth_http_begin();
void eth_http_handle();       // call from the comms task
void eth_http_topup();        // keep an HTTP listener socket ready
