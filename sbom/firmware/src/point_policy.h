// Portable project-owned point allowlist. This is an additional network filter,
// not authentication, routing, a mode change, or permission to write a read-only point.
#pragma once
#include "plc_point_map.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace point_policy {
constexpr unsigned RuleCapacity = 16;
enum class Kind : uint8_t { Coil, Register, DI, MX, IW };
enum Access : uint8_t { Read = 1, Write = 2, ReadWrite = 3 };
struct Rule {
    Kind kind = Kind::Coil;
    uint16_t index = 0;
    uint32_t host = 0; // dotted octets a.b.c.d are the number (a<<24)|(b<<16)|(c<<8)|d
    uint8_t access = 0;
};
struct Config {
    bool enabled = false;
    unsigned count = 0;
    Rule rules[RuleCapacity]{};
};

inline unsigned capacity(Kind kind) {
    switch (kind) {
    case Kind::Coil: return 512;
    case Kind::Register: return 256;
    case Kind::DI: return 8;
    case Kind::MX: return 32;
    case Kind::IW: return 8;
    }
    return 0;
}
inline bool input_mirror(Kind kind, uint16_t index) {
    return kind == Kind::Coil && index >= pointmap::DI_MIRROR_BASE && index < pointmap::DI_MIRROR_BASE + 8;
}
inline void canonicalize(Kind& kind, uint16_t& index) {
    if (input_mirror(kind, index)) { kind = Kind::DI; index -= pointmap::DI_MIRROR_BASE; }
}
inline bool parse_point(const char* text, Kind& kind, uint16_t& index) {
    if (!text || strlen(text) < 3 || strlen(text) > 7) return false;
    Kind parsed; const char* number;
    if (!strncmp(text, "COIL", 4)) { parsed = Kind::Coil; number = text + 4; }
    else if (!strncmp(text, "REG", 3)) { parsed = Kind::Register; number = text + 3; }
    else if (!strncmp(text, "DI", 2)) { parsed = Kind::DI; number = text + 2; }
    else if (!strncmp(text, "MX", 2)) { parsed = Kind::MX; number = text + 2; }
    else if (!strncmp(text, "IW", 2)) { parsed = Kind::IW; number = text + 2; }
    else return false;
    if (!*number || (*number == '0' && number[1])) return false;
    unsigned value = 0;
    for (const char* p = number; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10 + unsigned(*p - '0');
        if (value >= capacity(parsed)) return false;
    }
    // The raw Modbus mirror and named input are one point, not two ACL entries.
    // Rules use DI; checks below normalize protocol reads of COIL100..107.
    if (input_mirror(parsed, uint16_t(value))) return false;
    kind = parsed; index = uint16_t(value); return true;
}
inline bool format_point(Kind kind, uint16_t index, char* out, size_t size) {
    if (!out || index >= capacity(kind)) return false;
    canonicalize(kind, index);
    const char* prefix = kind == Kind::Coil ? "COIL" : kind == Kind::Register ? "REG" :
                         kind == Kind::DI ? "DI" : kind == Kind::MX ? "MX" : "IW";
    const int n = snprintf(out, size, "%s%u", prefix, unsigned(index));
    return n > 0 && size_t(n) < size;
}
inline bool parse_ipv4(const char* text, uint32_t& out) {
    if (!text || strlen(text) < 7 || strlen(text) > 15) return false;
    uint32_t result = 0; const char* p = text;
    for (unsigned part = 0; part < 4; ++part) {
        if (*p < '0' || *p > '9') return false;
        const char* start = p; unsigned octet = 0;
        while (*p >= '0' && *p <= '9') {
            octet = octet * 10 + unsigned(*p++ - '0');
            if (octet > 255 || p - start > 3) return false;
        }
        if (p - start > 1 && *start == '0') return false;
        result = (result << 8) | octet;
        if (part < 3) { if (*p++ != '.') return false; }
        else if (*p) return false;
    }
    out = result; return true;
}
inline bool format_ipv4(uint32_t host, char* out, size_t size) {
    if (!out) return false;
    const int n = snprintf(out, size, "%u.%u.%u.%u", unsigned(host >> 24),
                           unsigned((host >> 16) & 255), unsigned((host >> 8) & 255), unsigned(host & 255));
    return n > 0 && size_t(n) < size;
}
inline uint8_t parse_access(const char* text) {
    return !text ? 0 : !strcmp(text, "r") ? Read : !strcmp(text, "w") ? Write : !strcmp(text, "rw") ? ReadWrite : 0;
}
inline const char* access_name(uint8_t access) { return access == Read ? "r" : access == Write ? "w" : access == ReadWrite ? "rw" : ""; }
inline const char* validate(const Config& config) {
    if (config.count > RuleCapacity) return "host_acl rule capacity (0..16)";
    for (unsigned i = 0; i < config.count; ++i) {
        const auto& rule = config.rules[i];
        if (rule.index >= capacity(rule.kind) || input_mirror(rule.kind, rule.index)) return "host_acl canonical point (use DI0..7 for COIL100..107)";
        if (rule.access != Read && rule.access != Write && rule.access != ReadWrite) return "host_acl access r, w or rw required";
        for (unsigned j = 0; j < i; ++j) {
            const auto& prior = config.rules[j];
            if (rule.host == prior.host && rule.kind == prior.kind && rule.index == prior.index) return "duplicate host_acl host/point";
        }
    }
    return nullptr;
}
inline bool host_allowed(const Config& config, uint32_t host) {
    if (!config.enabled) return true;
    if (config.count > RuleCapacity) return false;
    for (unsigned i = 0; i < config.count; ++i) if (config.rules[i].host == host && config.rules[i].access) return true;
    return false;
}
inline bool allows(const Config& config, uint32_t host, Kind kind, uint16_t index, bool write) {
    if (!config.enabled) return true;
    if (config.count > RuleCapacity || index >= capacity(kind)) return false;
    canonicalize(kind, index);
    const uint8_t requested = write ? Write : Read;
    for (unsigned i = 0; i < config.count; ++i) {
        const auto& rule = config.rules[i];
        if (rule.host == host && rule.kind == kind && rule.index == index && (rule.access & requested)) return true;
    }
    return false;
}
inline bool range_allowed(const Config& config, uint32_t host, Kind kind, uint16_t first, uint16_t count, bool write) {
    if (!config.enabled) return true;
    if (!count || uint32_t(first) + count > capacity(kind)) return false;
    for (uint32_t index = first; index < uint32_t(first) + count; ++index)
        if (!allows(config, host, kind, uint16_t(index), write)) return false;
    return true;
}
inline bool all_points_allowed(const Config& config, uint32_t host, bool write) {
    if (!config.enabled) return true;
    const Kind kinds[] = {Kind::Coil, Kind::Register, Kind::DI, Kind::MX, Kind::IW};
    for (auto kind : kinds)
        if (!range_allowed(config, host, kind, 0, uint16_t(capacity(kind)), write)) return false;
    return true;
}
} // namespace point_policy
