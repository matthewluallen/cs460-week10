#include "diagnostic_log.h"
#include "plc_point_map.h"
#include "mqtt_sparkplug.h"
#include "modbus_tcp_server.h"
#include "svc_config.h"
#include "plc_core.h"
#include "net_types.h"
#include "mqtt_runtime.h"
#include "mqtt_sensors.h"
#include "tag_arbiter.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Sparkplug B protobuf (Eclipse Tahu org.eclipse.tahu.protobuf.Payload).
// Hand-encoded to keep the ESP32 dependency-free; the byte layout is validated
// against the official schema (protobuf ParseFromString round-trips every field).
//   Payload  : timestamp[1] varint · metrics[2] repeated msg · seq[3] varint
//   Metric   : name[1] string · datatype[4] varint · value oneof
//   value    : int_value[10] varint (datatype 3=Int32) · boolean_value[14] varint (11=Boolean)
// ---------------------------------------------------------------------------
static size_t pb_varint(uint8_t* b, uint64_t v) {
    size_t n = 0;
    do { uint8_t x = v & 0x7F; v >>= 7; b[n++] = v ? (x | 0x80) : x; } while (v);
    return n;
}
static size_t pb_key(uint8_t* b, uint32_t field, uint32_t wt) { return pb_varint(b, (field << 3) | wt); }

// append a length-delimited field (wire type 2)
static size_t pb_bytes(uint8_t* b, uint32_t field, const uint8_t* data, size_t len) {
    size_t n = pb_key(b, field, 2);
    n += pb_varint(b + n, len);
    memcpy(b + n, data, len);
    return n + len;
}
static size_t pb_uvarint_field(uint8_t* b, uint32_t field, uint64_t v) {
    size_t n = pb_key(b, field, 0);
    return n + pb_varint(b + n, v);
}

// one Metric -> out; SPB datatype 11=Boolean, 3=Int32.
static size_t sp_metric_bool(uint8_t* out, const char* name, bool val) {
    size_t n = pb_bytes(out, 1, (const uint8_t*)name, strlen(name));   // name [1]
    n += pb_uvarint_field(out + n, 4, 11);                             // datatype [4] Boolean
    n += pb_uvarint_field(out + n, 14, val ? 1 : 0);                   // boolean_value [14]
    return n;
}
static size_t sp_metric_int(uint8_t* out, const char* name, int32_t val) {
    size_t n = pb_bytes(out, 1, (const uint8_t*)name, strlen(name));
    n += pb_uvarint_field(out + n, 4, 3);                             // datatype [4] Int32
    n += pb_uvarint_field(out + n, 10, (uint32_t)val);                // int_value [10]
    return n;
}

// Build a full DDATA/DBIRTH Payload from the current I/O image.
static size_t sp_build_payload(uint8_t* buf, uint64_t ts_ms, uint32_t seq) {
    size_t n = pb_uvarint_field(buf, 1, ts_ms);                       // timestamp [1]
    uint8_t m[96];
    for (const auto& metric : pointmap::SPARKPLUG_METRICS) {
        uint16_t raw = metric.coil ? modbus_server.get_coil(metric.address) : modbus_server.get_register(metric.address);
        int32_t value = metric.signed_value ? int16_t(raw) : raw;
        size_t ml = metric.coil ? sp_metric_bool(m, metric.name, raw != 0)
                               : sp_metric_int(m, metric.name, value);
        n += pb_bytes(buf + n, 2, m, ml);
    }
    n += pb_uvarint_field(buf + n, 3, seq);                          // seq [3]
    return n;
}

// ---------------------------------------------------------------------------
static uint32_t s_connection = 0;
static char     s_broker[64] = {0};
static uint16_t s_port = 1883;
static char     s_group[24] = "TBD";
static char     s_node[24]  = "stamplc";
static bool     s_enabled = false;
static uint32_t s_seq = 0;
static uint32_t s_lastPub = 0;
static uint32_t s_lastTry = 0;
static uint32_t s_lastRgb = 0;

// Publish the ladder-driven display color (relay0->R, relay1->G, relay2->B) so a phone that
// subscribes to stamplc/display/rgb paints its background from the PLC logic. Retained so a
// freshly-connected phone gets the current color immediately.
static void sp_publish_rgb() {
    uint8_t r = modbus_server.get_coil(1) ? 255 : 0;   // RELAY0 -> R
    uint8_t g = modbus_server.get_coil(2) ? 255 : 0;   // RELAY1 -> G
    uint8_t b = modbus_server.get_coil(3) ? 255 : 0;   // RELAY2 -> B
    char j[48];
    int n = snprintf(j, sizeof(j), "{\"r\":%u,\"g\":%u,\"b\":%u}", r, g, b);
    mqtt_runtime_publish("stamplc/display/rgb", (const uint8_t*)j, n, 0, true);
}

static void topic(char* out, size_t n, const char* msgtype) {
    snprintf(out, n, "spBv1.0/%s/%s/%s", s_group, msgtype, s_node);   // .../<msgtype>/<edge>
}

// Legacy DCMD writes raw coil0; it is not the canonical base relay QX0 mapping.
static void on_message(const char* t, const uint8_t* payload, size_t len) {
    // Phone-sensor bridge: phone/<id>/{light,accel,orient} -> %IW input words.
    if (!strncmp(t, "phone/", 6)) { mqtt_sensors_ingest(t, payload, len); return; }
    if (!strstr(t, "/DCMD/")) return;
    if (g_bustrace) plc_bus_log(0x00, 'W', "Sparkplug DCMD (MQTT) -> relay", len, true);
    // Legacy teaching decoder: a boolean-value byte pattern writes raw coil0. A hardened
    // build would require broker auth + ACLs + Sparkplug host/state before acting.
    for (unsigned int i = 0; i + 1 < len; i++) {
        if (payload[i] == 0x70) {                                     // boolean_value [14] key
            // P3 arbiter: MQTT runs on core 1; take the image lock so note+apply are atomic
            // vs. the core-0 Modbus writer. A Modbus client writing coil 0 in the same scan
            // now raises TAG_CONFLICT instead of silently last-writer-wins.
            plc_image_lock();
            if (arb::note_write(arb::coil_key(pointmap::ESTOP), arb::SRC_MQTT))
                modbus_server.set_coil(0, payload[i + 1] ? true : false);
            plc_image_unlock();
            break;
        }
    }
}

static bool sp_publish(const char* msgtype) {
    uint8_t buf[512];
    size_t n = sp_build_payload(buf, (uint64_t)millis(), s_seq++);
    char t[64]; topic(t, sizeof(t), msgtype);
    return mqtt_runtime_publish(t, buf, n, 1, false) >= 0;
}

void mqtt_sparkplug_begin(const char* host, uint16_t port, const char* group, const char* node) {
    strncpy(s_broker, host ? host : "", sizeof(s_broker) - 1);
    s_port = port ? port : 1883;
    if (group && group[0]) strncpy(s_group, group, sizeof(s_group) - 1);
    if (node && node[0])   strncpy(s_node,  node,  sizeof(s_node) - 1);
    // Safety: never attempt to connect to the unconfigured placeholder broker.
    // A blocking connect() to an unreachable host on the display-owning loop is
    // what blanks the LCD; require a real broker before enabling.
    if (!s_broker[0] || !strcmp(s_broker, "mqtt.local")) {
        s_enabled = false;
        Serial.println("[PROTO] MQTT/Sparkplug: no broker set (MQTT_BROKER_HOST is the placeholder) — not connecting.");
        return;
    }
    char willTopic[64]; topic(willTopic, sizeof(willTopic), "NDEATH");
    char cid[40]; snprintf(cid, sizeof(cid), "stamplc-%s", s_node);
    // Legacy Sparkplug application profile remains separately unqualified.
    static const uint8_t will[] = {'o','f','f','l','i','n','e'};
    s_enabled = mqtt_runtime_begin(s_broker,s_port,cid,willTopic,will,sizeof(will),on_message);
    s_seq = 0;
    Serial.printf("[PROTO] MQTT/Sparkplug %s -> broker %s:%u (group %s, node %s)\n",
                  s_enabled ? "client initialized (connection pending)" : "initialization failed",
                  s_broker, s_port, s_group, s_node);
}

bool mqtt_sparkplug_connected() { return s_enabled && mqtt_runtime_status().connected; }

void mqtt_sparkplug_poll() {
    if (!s_enabled) return;
    mqtt_runtime_poll();
    const auto state = mqtt_runtime_status();
    if (!state.connected) return;
    if (state.connections != s_connection) {
        s_connection = state.connections;
        char sub[64]; snprintf(sub, sizeof(sub), "spBv1.0/%s/DCMD/%s/#", s_group, s_node);
        mqtt_runtime_subscribe(sub, 2);
        mqtt_sensors_subscribe();          // phone/+/{light,accel,orient} -> %IW
        sp_publish("DBIRTH");
        diagnostic_log_printf(diagnostics::Origin::Mqtt,"[PROTO] MQTT3.1.1 session connected; resumed=%d\n", state.session_present);
    }
    const uint32_t now = millis();
    if (now - s_lastPub >= 2000) { s_lastPub = now; sp_publish("DDATA"); }
    if (now - s_lastRgb >= 400) { s_lastRgb = now; sp_publish_rgb(); }   // logic -> phone background
}
