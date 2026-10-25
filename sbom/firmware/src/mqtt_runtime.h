#pragma once
#include <Arduino.h>

// The MQTT protocol engine is ESP-MQTT from the pinned Arduino/IDF SDK.
// Application profiles (JSON or Sparkplug) remain separate from MQTT semantics.
using MqttMessageHandler=void (*)(const char*,const uint8_t*,size_t);
struct MqttStatus {
    bool started,connected,session_present;
    uint32_t connections,disconnections,delivered,received,dropped,errors;
    int last_message_id;
};
bool mqtt_runtime_begin(const char* host,uint16_t port,const char* client_id,
                        const char* will_topic,const uint8_t* will,size_t will_len,
                        MqttMessageHandler callback);
void mqtt_runtime_poll(); // drain complete messages on the application owner
MqttStatus mqtt_runtime_status();
int mqtt_runtime_publish(const char* topic,const uint8_t* data,size_t size,int qos,bool retain);
int mqtt_runtime_subscribe(const char* topic,int qos);
int mqtt_runtime_unsubscribe(const char* topic);
