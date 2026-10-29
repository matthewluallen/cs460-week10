#include "mqtt_sensors.h"
#include "mqtt_runtime.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

namespace {
int32_t  s_light = 0, s_accel_x100 = 0, s_tilt = 0;
uint32_t s_last = 0;
bool     s_have = false;

// Minimal, allocation-free JSON scalar extractor: find "<key>" then parse the number that
// follows a ':' . Only what a phone publishes ({"lux":..},{"mag":..},{"beta":..}); not a
// general JSON parser. Returns false if the key or a number isn't found.
bool json_num(const char* s, size_t n, const char* key, double& out) {
    char pat[24];
    int pl = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (pl <= 0 || (size_t)pl >= n) return false;
    for (size_t i = 0; i + (size_t)pl < n; i++) {
        if (memcmp(s + i, pat, pl)) continue;
        size_t j = i + pl;
        while (j < n && (s[j] == ' ' || s[j] == ':')) j++;
        char buf[32]; size_t k = 0;
        while (j < n && k < sizeof(buf) - 1 &&
               (isdigit((unsigned char)s[j]) || s[j] == '-' || s[j] == '+' ||
                s[j] == '.' || s[j] == 'e' || s[j] == 'E')) buf[k++] = s[j++];
        buf[k] = 0;
        if (!k) return false;
        out = atof(buf);
        return true;
    }
    return false;
}
int16_t clamp16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }
}

void mqtt_sensors_ingest(const char* topic, const uint8_t* payload, size_t len) {
    if (!topic || !payload || !len) return;
    const char* sl = strrchr(topic, '/');
    if (!sl) return;
    const char* sensor = sl + 1;
    const char* s = (const char*)payload;
    double v;
    if (!strcmp(sensor, "light")) {
        if (json_num(s, len, "lux", v)) { s_light = (int32_t)lround(v); s_have = true; s_last = millis(); }
    } else if (!strcmp(sensor, "accel")) {
        if (json_num(s, len, "mag", v)) { s_accel_x100 = (int32_t)lround(v * 100.0); s_have = true; s_last = millis(); }
    } else if (!strcmp(sensor, "orient")) {
        if (json_num(s, len, "beta", v)) { s_tilt = (int32_t)lround(v); s_have = true; s_last = millis(); }
    }
}

void mqtt_sensors_subscribe() {
    mqtt_runtime_subscribe("phone/+/light", 0);
    mqtt_runtime_subscribe("phone/+/accel", 0);
    mqtt_runtime_subscribe("phone/+/orient", 0);
}

bool mqtt_sensors_fresh() { return s_have && (millis() - s_last < 8000); }
int16_t mqtt_sensors_light_x1()       { return clamp16(s_light); }
int16_t mqtt_sensors_accel_mag_x100() { return clamp16(s_accel_x100); }
int16_t mqtt_sensors_tilt_beta()      { return clamp16(s_tilt); }
