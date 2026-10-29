// Phone-sensor MQTT ingest -> %IW input words.
//
// The StampLC subscribes to phone/+/{light,accel,orient} on the lab MQTT broker (the
// phone-sensor bridge). Incoming JSON payloads are parsed to a scalar and exposed as
// input words so ladder logic can react to a phone's light / motion / tilt. Values age
// out so a disconnected phone doesn't freeze the process image.
#pragma once
#include <stdint.h>
#include <stddef.h>

// Parse one phone/<id>/<sensor> message and store the latest scalar. Safe to call from
// the MQTT delivery path (loop task).
void mqtt_sensors_ingest(const char* topic, const uint8_t* payload, size_t len);

// Subscribe to the phone sensor topics. Call once per MQTT (re)connection.
void mqtt_sensors_subscribe();

// True while at least one phone value has arrived within the freshness window.
bool mqtt_sensors_fresh();

// Latest values (clamped to int16). light = lux; accel = |a|*100 (m/s^2*100); tilt = beta deg.
int16_t mqtt_sensors_light_x1();
int16_t mqtt_sensors_accel_mag_x100();
int16_t mqtt_sensors_tilt_beta();
