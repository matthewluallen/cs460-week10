// eth_ota.h - W5500-native unsigned-OTA upload listener on port 80.
// Serves the firmware-update endpoint over the W5500's own TCP stack
// (Ethernet_Generic) so the QEMU twin and real Ethernet builds expose it.
// ota_handler.cpp (the Arduino WiFi WebServer version) is kept as a source-
// analysis teaching artifact but never binds on the W5500-only build.
// Guarded by ENABLE_ETHERNET. Serviced from the core-0 comms task.
#pragma once
#include <Arduino.h>
void eth_ota_begin();     // start the :80 OTA listener
void eth_ota_handle();    // service from the comms task
void eth_ota_topup();     // keep an OTA listener socket ready
