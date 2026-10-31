#include "mqtt_sensors.h"
#include "mqtt_runtime.h"
#include "modbus_tcp_server.h"
#include <Arduino.h>
#include <string.h>

namespace {
phone::State s;
// Ingest runs on the MQTT delivery path (loop task); the scan, console and Modbus mirror
// read the same state. The recursive image lock keeps every reader consistent.
struct Guard { Guard() { plc_image_lock(); } ~Guard() { plc_image_unlock(); } };
}

void mqtt_sensors_ingest(const char* topic, const uint8_t* payload, size_t len) {
    Guard guard;
    s.ingest(topic, payload, len, millis());
}

void mqtt_sensors_subscribe() {
    // One wildcard subscription per published topic. The id is checked in ingest, so a
    // pinned controller still sees (and counts) other phones' messages as rejected.
    for (unsigned i = 0; i < phone::Count; ++i) {
        const char* topic = pointmap::PHONE_POINTS[i].topic;
        bool repeat = false;
        for (unsigned j = 0; j < i; ++j) if (!strcmp(pointmap::PHONE_POINTS[j].topic, topic)) repeat = true;
        if (repeat) continue;
        char filter[40];
        snprintf(filter, sizeof(filter), "phone/+/%s", topic);
        mqtt_runtime_subscribe(filter, 0);
    }
}

void mqtt_sensors_words(int32_t (&words)[phone::WordCount], uint32_t now) {
    Guard guard;
    s.words(words, now);
}

void mqtt_sensors_publish_registers(uint32_t now) {
    Guard guard;
    for (unsigned i = 0; i < phone::Count; ++i) {
        const auto& def = pointmap::PHONE_POINTS[i];
        const int32_t v = s.value(i, now);
        if (def.words == 2) {
            const uint32_t bits = uint32_t(v);
            modbus_server.set_register(def.reg, uint16_t(bits >> 16));
            modbus_server.set_register(def.reg + 1, uint16_t(bits));
        } else modbus_server.set_register(def.reg, def.signed_value ? phone::reg_s16(v) : phone::reg_u16(v));
    }
    const uint32_t fresh = s.fresh_mask(now), seen = s.seen_mask();
    modbus_server.set_register(pointmap::PHONE_FRESH_LO, uint16_t(fresh));
    modbus_server.set_register(pointmap::PHONE_FRESH_HI, uint16_t(fresh >> 16));
    modbus_server.set_register(pointmap::PHONE_SEEN_LO, uint16_t(seen));
    modbus_server.set_register(pointmap::PHONE_SEEN_HI, uint16_t(seen >> 16));
    modbus_server.set_register(pointmap::PHONE_MSGS_ACCEPTED, uint16_t(s.accepted));
    modbus_server.set_register(pointmap::PHONE_MSGS_REJECTED, uint16_t(s.rejected()));
    modbus_server.set_register(pointmap::PHONE_PINNED, s.pinned[0] ? 1 : 0);
}

bool mqtt_sensors_pin(const char* id) { Guard guard; return s.pin(id); }
void mqtt_sensors_unpin() { Guard guard; s.unpin(); }

String mqtt_sensors_status_json(bool connected, const char* broker, uint32_t received, uint32_t dropped) {
    // Snapshot under the lock; the caller writes the reply without holding it.
    phone::State copy;
    const uint32_t now = millis();
    { Guard guard; copy = s; }
    String out;
    out.reserve(2400);
    char b[200];
    snprintf(b, sizeof(b), "{\"mqtt\":\"status\",\"connected\":%s,\"broker\":\"%s\",\"authentication\":\"none\",",
             connected ? "true" : "false", broker && broker[0] ? broker : "");
    out += b;
    if (copy.pinned[0]) { snprintf(b, sizeof(b), "\"pinned\":\"%s\",\"accepts\":\"pinned phone id only (a claim, not proof)\",", copy.pinned); out += b; }
    else out += "\"pinned\":null,\"accepts\":\"any phone id (last writer wins)\",";
    snprintf(b, sizeof(b), "\"last_id\":\"%s\",\"accepted\":%lu,\"rejected\":{\"other_id\":%lu,\"bad_id\":%lu,\"no_field\":%lu},",
             copy.last_id, (unsigned long)copy.accepted, (unsigned long)copy.other_id,
             (unsigned long)copy.bad_id, (unsigned long)copy.no_field);
    out += b;
    snprintf(b, sizeof(b), "\"mqtt_received\":%lu,\"mqtt_dropped\":%lu,\"stale_ms\":%lu,\"regs\":\"%u-%u\",\"points\":{",
             (unsigned long)received, (unsigned long)dropped, (unsigned long)pointmap::PHONE_STALE_MS,
             unsigned(pointmap::PHONE_POINTS[0].reg), unsigned(pointmap::PHONE_PINNED));
    out += b;
    for (unsigned i = 0; i < phone::Count; ++i) {
        const auto& def = pointmap::PHONE_POINTS[i];
        const auto& p = copy.points[i];
        char age[16];
        if (p.seen) snprintf(age, sizeof(age), "%lu", (unsigned long)(now - p.last_ms)); else strcpy(age, "null");
        snprintf(b, sizeof(b), "%s\"%s\":{\"v\":%ld,\"fresh\":%d,\"age_ms\":%s,\"reg\":%u,\"unit\":\"%s\"}",
                 i ? "," : "", def.name, (long)copy.value(i, now), copy.fresh(i, now) ? 1 : 0, age, unsigned(def.reg), def.unit);
        out += b;
    }
    out += "}}";
    return out;
}
