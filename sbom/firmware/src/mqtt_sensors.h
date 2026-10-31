// Phone-sensor MQTT ingest -> network-only phone points.
//
// The StampLC subscribes to phone/+/<topic> for every topic the phone page publishes
// (light, accel, gyro, orient, sound, geo, battery, btn1, btn2) on the lab MQTT broker.
// Each scalar becomes one phone point (point_map.json `phone_points`) with its own 8 s
// freshness timer. Phone points are NOT the physical input image: they never write
// %IX / IN1-IN8 / the DI mirror coils. A project reads them only through explicit word
// sources (phone_<name>, phone_<name>_fresh); every point is also mirrored to Modbus
// holding registers 140-165 and printed by `mqtt status`. Authentication: none.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <WString.h>
#include "mqtt_sensors_core.h"

// Parse one phone/<id>/<topic> message (MQTT delivery path, loop task).
void mqtt_sensors_ingest(const char* topic, const uint8_t* payload, size_t len);

// Subscribe to the phone topics. Call once per MQTT (re)connection.
void mqtt_sensors_subscribe();

// Project word-source image: words[2i] = point i value, words[2i+1] = 1 while fresh.
void mqtt_sensors_words(int32_t (&words)[phone::WordCount], uint32_t now);

// Mirror every phone point and the status words into the Modbus holding registers.
// Caller holds the PLC image lock (the scan does).
void mqtt_sensors_publish_registers(uint32_t now);

// Accept only one phone id (identity by claim in the topic; there is no authentication).
// Returns false for an invalid id. A pin change resets every point to "missing".
bool mqtt_sensors_pin(const char* id);
void mqtt_sensors_unpin();

// One JSON line for `mqtt status`: connection, pin, counters and every point.
// received/dropped: the MQTT runtime's delivery counters (a full queue drops messages).
String mqtt_sensors_status_json(bool connected, const char* broker, uint32_t received, uint32_t dropped);
