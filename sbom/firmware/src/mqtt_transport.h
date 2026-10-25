#pragma once
#include <Arduino.h>
#include "esp_transport.h"

// ESP-MQTT's task hands byte operations to the W5500-owning comms task.
// The caller never touches SPI. WiFi/lwIP uses ESP-MQTT's standard transport.
esp_transport_handle_t mqtt_w5500_transport();
void mqtt_w5500_service();
