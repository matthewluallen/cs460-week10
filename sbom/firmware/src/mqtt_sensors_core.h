// Network-only phone points: pure state + parsing. No Arduino, MQTT, Modbus or RTOS.
//
// A phone publishes phone/<id>/<topic> with a small JSON object (mqtt/public/index.html).
// Every scalar the page sends is a point in point_map.json `phone_points`. Each point has
// its OWN freshness timer (PHONE_STALE_MS). Points live outside the physical input image
// (IN1-IN8 / %IX / DI mirror coils): a project reads them only through explicit word
// sources phone_<name> and phone_<name>_fresh.
//
// Honesty notes the lab relies on:
//  - "fresh" means a message for that point ARRIVED recently. It says nothing about whether
//    the value is true: an Override/spoof from the page is fresh and indistinguishable.
//  - Pinning accepts one phone id. The id is a claim in the topic, not authentication.
//  - When a point goes stale its value holds the last sample (fresh = 0 tells the ladder),
//    except momentary buttons, which read 0 so a lost "release" never holds an input on.
#pragma once
#include "plc_point_map.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace phone {
constexpr unsigned Count = pointmap::PHONE_POINT_COUNT;
constexpr unsigned IdCapacity = 32;          // ids are 1..31 chars of [A-Za-z0-9_-]
constexpr unsigned WordCount = 2 * Count;    // (value, fresh) per point
static_assert(Count <= 32, "fresh/seen bitmasks are two 16-bit registers");

struct Point { int32_t value = 0; uint32_t last_ms = 0; bool seen = false; };
enum class Verdict : uint8_t { Accepted, NotPhone, BadId, OtherId, NoField };

inline bool valid_id(const char* s, size_t n) {
    if (!s || !n || n >= IdCapacity) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

// Minimal, allocation-free extractor for the phone page's flat JSON objects: finds "key"
// and parses the value that follows as a number, true or false. null, strings and
// anything else are "no value" (false). Not a general JSON parser.
inline bool json_field(const char* s, size_t n, const char* key, double& out) {
    char pat[24];
    const size_t kl = strlen(key);
    if (!s || kl + 2 >= sizeof(pat)) return false;
    pat[0] = '"'; memcpy(pat + 1, key, kl); pat[kl + 1] = '"'; pat[kl + 2] = 0;
    const size_t pl = kl + 2;
    for (size_t i = 0; i + pl <= n; ++i) {
        if (memcmp(s + i, pat, pl)) continue;
        size_t j = i + pl;
        while (j < n && (s[j] == ' ' || s[j] == '\t')) ++j;
        if (j >= n || s[j] != ':') continue;           // a value string that happens to match
        ++j;
        while (j < n && (s[j] == ' ' || s[j] == '\t')) ++j;
        if (j + 4 <= n && !memcmp(s + j, "true", 4)) { out = 1; return true; }
        if (j + 5 <= n && !memcmp(s + j, "false", 5)) { out = 0; return true; }
        char buf[32]; size_t k = 0; bool digit = false;
        while (j < n && k < sizeof(buf) - 1) {
            const char c = s[j];
            if (c >= '0' && c <= '9') digit = true;
            else if (c != '-' && c != '+' && c != '.' && c != 'e' && c != 'E') break;
            buf[k++] = c; ++j;
        }
        buf[k] = 0;
        if (!digit) return false;
        char* end = nullptr;
        const double v = strtod(buf, &end);
        if (!end || *end || !isfinite(v)) return false;
        out = v; return true;
    }
    return false;
}

inline int32_t scaled(double v, int32_t scale) {
    const double x = v * double(scale);
    if (!(x == x)) return 0;
    if (x >= 2147483647.0) return 2147483647;
    if (x <= -2147483648.0) return INT32_MIN;
    return int32_t(lround(x));
}

struct State {
    Point points[Count];
    char pinned[IdCapacity] = {};    // empty: any phone id is accepted (last writer wins)
    char last_id[IdCapacity] = {};
    uint32_t accepted = 0, other_id = 0, bad_id = 0, no_field = 0;

    uint32_t rejected() const { return other_id + bad_id + no_field; }
    void reset_points() { for (auto& p : points) p = Point{}; }
    // A pin change starts from "missing": data from the previous phone must not carry over.
    bool pin(const char* id) {
        const size_t n = id ? strlen(id) : 0;
        if (!valid_id(id, n)) return false;
        memcpy(pinned, id, n + 1); reset_points(); return true;
    }
    void unpin() { pinned[0] = 0; reset_points(); }
    bool fresh(unsigned i, uint32_t now) const {
        return i < Count && points[i].seen && uint32_t(now - points[i].last_ms) < pointmap::PHONE_STALE_MS;
    }
    int32_t value(unsigned i, uint32_t now) const {
        if (i >= Count || !points[i].seen) return 0;
        if (pointmap::PHONE_POINTS[i].kind == pointmap::PhoneKind::Momentary && !fresh(i, now)) return 0;
        return points[i].value;
    }
    void words(int32_t (&out)[WordCount], uint32_t now) const {
        for (unsigned i = 0; i < Count; ++i) { out[2 * i] = value(i, now); out[2 * i + 1] = fresh(i, now) ? 1 : 0; }
    }
    uint32_t fresh_mask(uint32_t now) const {
        uint32_t m = 0; for (unsigned i = 0; i < Count; ++i) if (fresh(i, now)) m |= 1u << i; return m;
    }
    uint32_t seen_mask() const {
        uint32_t m = 0; for (unsigned i = 0; i < Count; ++i) if (points[i].seen) m |= 1u << i; return m;
    }
    // topic = phone/<id>/<topic>. Only the topics in PHONE_POINTS carry data.
    Verdict ingest(const char* topic, const uint8_t* payload, size_t len, uint32_t now) {
        if (!topic || strncmp(topic, "phone/", 6)) return Verdict::NotPhone;
        const char* id = topic + 6;
        const char* slash = strchr(id, '/');
        if (!slash || strchr(slash + 1, '/') || !slash[1]) return Verdict::NotPhone;
        const size_t idn = size_t(slash - id);
        if (!valid_id(id, idn)) { ++bad_id; return Verdict::BadId; }
        if (pinned[0] && (strlen(pinned) != idn || memcmp(pinned, id, idn))) { ++other_id; return Verdict::OtherId; }
        const char* sensor = slash + 1;
        const char* body = reinterpret_cast<const char*>(payload);
        unsigned updated = 0;
        for (unsigned i = 0; payload && i < Count; ++i) {
            const auto& def = pointmap::PHONE_POINTS[i];
            if (strcmp(sensor, def.topic)) continue;
            double v;
            if (!json_field(body, len, def.key, v)) continue;
            points[i].value = def.kind == pointmap::PhoneKind::Analog ? scaled(v, def.scale) : (v != 0 ? 1 : 0);
            points[i].last_ms = now; points[i].seen = true; ++updated;
        }
        if (!updated) { ++no_field; return Verdict::NoField; }
        ++accepted; memcpy(last_id, id, idn); last_id[idn] = 0;
        return Verdict::Accepted;
    }
};

// Modbus mirror encodings (the VM always sees the full int32).
inline uint16_t reg_u16(int32_t v) { return uint16_t(v < 0 ? 0 : v > 65535 ? 65535 : v); }
inline uint16_t reg_s16(int32_t v) { return uint16_t(int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v)); }
} // namespace phone
