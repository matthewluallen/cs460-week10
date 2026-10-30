// Network adapters supply the actual socket peer; audit text and forwarded
// headers never grant point access. The optional project policy is additional
// to each adapter's existing mode, writable-point and build-profile checks.
#pragma once
#include <stdint.h>
#include <string.h>
#include <initializer_list>
#include "plc_project.h"
#include "plc_tags.h"

void plc_image_lock();
void plc_image_unlock();
struct PlcNetworkImageGuard {
    PlcNetworkImageGuard() { plc_image_lock(); }
    ~PlcNetworkImageGuard() { plc_image_unlock(); }
};

template<class IP> inline uint32_t plc_network_ipv4(const IP& ip) {
    return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) |
           (uint32_t(ip[2]) << 8) | uint32_t(ip[3]);
}

struct PlcAccessPeer {
    enum class Origin : uint8_t { Unknown, TrustedSerial, IPv4 };
    Origin origin = Origin::Unknown;
    uint32_t ipv4 = 0;
    static PlcAccessPeer network(uint32_t ip) { return {Origin::IPv4, ip}; }
    static PlcAccessPeer serial() { return {Origin::TrustedSerial, 0}; }
};

inline bool plc_network_range_allowed(const PlcAccessPeer& peer,
        point_policy::Kind kind, uint16_t first, uint16_t count, bool write) {
    if (!plc_project_host_acl_enabled()) return true;
    // USB and wired RS-485 have no IP peer. Their physical/gateway trust boundary
    // is explicit; an unknown network origin never inherits this exemption.
    if (peer.origin == PlcAccessPeer::Origin::TrustedSerial) return true;
    return peer.origin == PlcAccessPeer::Origin::IPv4 &&
        plc_project_range_allowed(peer.ipv4, kind, first, count, write);
}

inline point_policy::Kind plc_network_tag_kind(TagKind kind) {
    switch (kind) {
    case TK_COIL: return point_policy::Kind::Coil;
    case TK_DI: return point_policy::Kind::DI;
    case TK_MX: return point_policy::Kind::MX;
    case TK_IW: return point_policy::Kind::IW;
    default: return point_policy::Kind::Register;
    }
}

inline bool plc_network_tag_allowed(uint32_t peer, const TagDef* tag, bool write) {
    return tag && plc_project_point_allowed(peer, plc_network_tag_kind(tag->kind), tag->addr, write);
}

inline constexpr const char* PLC_ACL_ERROR = "{\"err\":\"host_point_acl_denied\"}";

// Point views enforce individual permissions below this route gate. Unfiltered
// snapshots/logs and network management are deliberately unavailable while the
// policy is enabled: a read-only point grant is not a controller-admin grant.
inline bool plc_network_http_allowed(uint32_t peer, const char* path, bool mutation = false) {
    if (!plc_project_host_acl_enabled()) return true;
    if (!path || !plc_project_host_allowed(peer)) return false;
    for (const char* point_path : {"/api/points", "/api/set", "/api/force", "/api/coils", "/api/registers"})
        if (!strcmp(path, point_path)) return true;
    if (mutation) return false;
    for (const char* metadata_path : {"/", "/index.html", "/api/mode", "/api/proto", "/api/sys", "/api/console", "/api/timing", "/api/config"})
        if (!strcmp(path, metadata_path)) return true;
    return false;
}

template<class Build> inline String plc_network_json(uint32_t peer, const char* path, Build build, bool mutation = false) {
    PlcNetworkImageGuard guard;
    if (!plc_network_http_allowed(peer, path, mutation)) return String(PLC_ACL_ERROR);
    return build();
}

// These two bounded legacy outstation profiles expose the same fixed snapshot:
// DI0..7, raw COIL0..3 and REG0/1/10/12 (not project QX ordinal addresses).
inline bool plc_network_legacy_snapshot_allowed(uint32_t peer) {
    using K = point_policy::Kind;
    return plc_project_range_allowed(peer, K::DI, 0, 8, false) &&
        plc_project_range_allowed(peer, K::Coil, 0, 4, false) &&
        plc_project_range_allowed(peer, K::Register, 0, 2, false) &&
        plc_project_point_allowed(peer, K::Register, 10, false) &&
        plc_project_point_allowed(peer, K::Register, 12, false);
}

// Deferred controller actions must belong to the project that admitted them.
// Call while holding the image lock immediately before the side effect.
inline bool plc_network_deferred_allowed(uint32_t peer, const char* path, uint32_t generation) {
    return plc_project_generation() == generation && plc_network_http_allowed(peer, path, true);
}
