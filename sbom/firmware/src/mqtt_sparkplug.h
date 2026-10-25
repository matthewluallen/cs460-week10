// mqtt_sparkplug.h — MQTT / Sparkplug B northbound (LS-18): the IIoT surface.
//
// Publishes the StamPLC I/O image to an MQTT broker as authentic Sparkplug B
// (Eclipse Tahu protobuf: DBIRTH then DDATA) and subscribes to DCMD to accept
// commands — the unauthenticated IIoT control path the course teaches to defend
// (broker auth/TLS, ACLs, Sparkplug host/state). Bound to the same I/O image as
// the Modbus servers via modbus_server get/set. Enabled via SVC_MQTT.
//
// The Sparkplug B payload encoder is byte-validated against the official
// org.eclipse.tahu.protobuf.Payload schema (see protocol_lab: sparkplug decode).
#pragma once
#include <Arduino.h>

// host/port of the broker; group + node id form the Sparkplug topic namespace.
void mqtt_sparkplug_begin(const char* broker_host, uint16_t broker_port,
                          const char* group_id, const char* node_id);
void mqtt_sparkplug_poll();     // maintain session, publish DDATA, service DCMD; call from loop()
bool mqtt_sparkplug_connected();
