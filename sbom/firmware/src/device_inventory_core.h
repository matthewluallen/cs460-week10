// Bounded, allocation-free topology/identity discovery. No measurement or output driver.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mlx90614_protocol.h"

namespace devices {
enum class Profile : uint8_t { DirectEnv = 0, Observe = 1, PaHub70 = 0x70 };
inline bool valid_profile(uint8_t p) { return p <= 1 || (p >= 0x70 && p <= 0x77); }
inline bool parse_profile(const char* s, uint8_t& p) {
    if (!s) return false;
    if (!strcmp(s, "observe")) { p = 1; return true; }
    if (!strcmp(s, "direct-env")) { p = 0; return true; }
    if (strlen(s) == 7 && !strncmp(s, "pahub7", 6) && s[6] >= '0' && s[6] <= '7') {
        p = uint8_t(0x70 + s[6] - '0'); return true;
    }
    return false;
}
inline uint8_t crc8(const uint8_t* p, size_t n) {
    uint8_t c = 0xff;
    while (n--) { c ^= *p++; for (int b = 0; b < 8; ++b) c = (c & 0x80) ? uint8_t((c << 1) ^ 0x31) : uint8_t(c << 1); }
    return c;
}
// A declared ToF4M route permits the model-specific 16-bit identity transaction.
// Zero disables probing. High byte=mux, low byte=channel+1; address fixed at0x29.
inline bool valid_tof4m(uint16_t route) {
    return route==0 || ((route>>8)>=0x70 && (route>>8)<=0x77 &&
                        (route&255)>=1 && (route&255)<=6);
}
inline bool parse_tof4m(const char* text,uint16_t& route) {
    if(!text)return false;
    if(!strcmp(text,"none")){route=0;return true;}
    if(strlen(text)!=9 || strncmp(text,"pahub7",6) || text[6]<'0' || text[6]>'7' ||
       text[7]!=':' || text[8]<'0' || text[8]>'5')return false;
    route=uint16_t((0x70+text[6]-'0')<<8)|uint16_t(text[8]-'0'+1);return true;
}
struct Bus {
    virtual ~Bus() = default;
    virtual bool ack(uint8_t address) = 0;
    virtual bool write(uint8_t address, const uint8_t* data, size_t n) = 0;
    virtual bool read(uint8_t address, uint8_t* data, size_t n) = 0;
    virtual bool reg(uint8_t address, uint8_t reg, uint8_t* data, size_t n) = 0;
    virtual bool reg16(uint8_t, uint16_t, uint8_t*, size_t) { return false; }
};
enum class State : uint8_t { Unobserved, Missing, Unsupported, Identified, IoError, Conflict };
inline const char* state_name(State s) {
    switch(s) {
    case State::Missing:return "missing"; case State::Unsupported:return "unsupported";
    case State::Identified:return "identified"; case State::IoError:return "io_error";
    case State::Conflict:return "root_conflict"; default:return "unobserved";
    }
}
struct Device {
    uint8_t bus = 0, mux = 0, address = 0; // bus 0=Port A, 1=internal module bus
    int8_t channel = -1;
    State state = State::Unobserved;
    const char* driver = "unknown";
    uint16_t part = 0, revision = 0;
    uint64_t serial = 0; // Only populated by a CRC-qualified serial-number command.
    bool unique_serial = false;
};
class Inventory {
public:
    static constexpr size_t Capacity = 64;
    Device entries[Capacity]{};
    size_t count = 0;
    uint32_t generation = 0;
    bool complete = false, overflow = false, route_ok = true;
    bool root[128]{};
    void reset() { count = 0; }
    Device* record(uint8_t bus, uint8_t mux, int8_t channel, uint8_t address) {
        for (size_t i = 0; i < count; ++i) {
            auto& d = entries[i];
            if (d.bus == bus && d.mux == mux && d.channel == channel && d.address == address) {
                d = Device{}; d.bus = bus; d.mux = mux; d.channel = channel; d.address = address; return &d;
            }
        }
        if (count == Capacity) { overflow = true; return nullptr; }
        auto* d = &entries[count++]; *d = Device{};
        d->bus = bus; d->mux = mux; d->channel = channel; d->address = address; return d;
    }
    // One synchronous maintenance discovery. Deadline is enforced by transport;
    // each route is closed even on failure. It never calls a sensor's begin().
    void scan(Bus& external, Bus& internal, uint8_t profile, uint16_t tof4m=0) {
        ++generation; complete = false; overflow = false; route_ok = true;
        memset(root, 0, sizeof(root));
        for (size_t i = 0; i < count; ++i) entries[i].state = State::Unobserved;
        if (!valid_profile(profile) || !valid_tof4m(tof4m)) return;
        uint8_t mux = profile >= 0x70 ? profile : 0;
        if (mux && !select(external, mux, 0)) { route_ok = false; return; }
        // First establish upstream addresses. Never attribute an upstream device
        // to all six child routes, nor probe sensors behind a colliding root address.
        for (uint8_t a = 8; a < 0x78; ++a) root[a] = external.ack(a);
        bool possible_mux = false;
        for (uint8_t a = 0x70; a < 0x77; ++a) possible_mux |= root[a];
        for (uint8_t a = 8; a < 0x78; ++a) if (root[a]) {
            Device* d = record(0, 0, -1, a); if (!d) continue;
            d->state = State::Unsupported;
            if (a == mux) { d->driver = "pahub-configured"; d->state = State::Identified; }
            else if (profile != 1 && (a < 0x70 || (profile == 0 && !possible_mux && a == 0x77))) identify(external, *d);
        }
        if (mux) for (size_t i=0;i<count;++i) {
            auto& d=entries[i];
            if(d.bus==0 && d.mux==mux && d.channel>=0 && root[d.address]) d.state=State::Conflict;
        }
        if (mux) for (int8_t ch = 0; ch < 6; ++ch) {
            if (!select(external, mux, uint8_t(1u << ch))) { route_ok = false; break; }
            for (uint8_t a = 8; a < 0x78; ++a) {
                if (a == mux) continue; // Cannot observe a child hidden by its own mux address.
                if (root[a]) continue; // Upstream collision is inherently indistinguishable electrically.
                if (!external.ack(a)) continue;
                Device* d = record(0, mux, ch, a); if (!d) continue;
                d->state = State::Unsupported;
                // Nested hubs are not declared in this profile. Keep 0x70..0x76 opaque.
                if(a==0x29 && tof4m==uint16_t((mux<<8)|(ch+1))) {
                    uint8_t id[2]{};d->state=State::IoError;
                    // Manufacturer MODEL_ID/MODULE_TYPE, not unique identity.
                    if(external.reg16(a,0x010f,id,2)) {
                        d->part=uint16_t(id[0]<<8)|id[1];
                        d->state=d->part==0xeacc?State::Identified:State::Unsupported;
                        if(d->state==State::Identified)d->driver="vl53l1x-candidate";
                    }
                } else if (a < 0x70 || a == 0x77) identify(external, *d);
            }
            if (!select(external, mux, 0)) { route_ok = false; break; }
        }
        if (mux && !select(external, mux, 0)) route_ok = false;
        // Module protocol has no immutable chip ID. Label it a protocol candidate,
        // with separate transaction status, never a proven SKU/unique identity.
        for (uint8_t a = 0x20; a <= 0x2f; ++a) if (internal.ack(a)) {
            Device* d = record(1, 0, -1, a); if (!d) continue;
            uint8_t fw = 0, status = 0, addr = 0;
            d->driver = "stamplc-io-candidate"; d->state = State::IoError;
            if (internal.reg(a, 0xfe, &fw, 1) && internal.reg(a, 0xfb, &status, 1) && internal.reg(a, 0xff, &addr, 1)) {
                d->part = addr; d->revision = fw;
                d->state = fw != 0xff && addr == a && status <= 3 ? State::Identified : State::Unsupported;
            }
        }
        complete = route_ok && !overflow;
        if (complete) for (size_t i = 0; i < count; ++i)
            if (entries[i].state == State::Unobserved) entries[i].state = State::Missing;
    }
    static bool select(Bus& b, uint8_t mux, uint8_t mask) {
        uint8_t got = 0xff;
        return b.write(mux, &mask, 1) && b.read(mux, &got, 1) && got == mask;
    }
private:
    static void identify(Bus& b, Device& d) {
        uint8_t data[9]{};
        if (d.address == 0x57) {
            d.state = State::IoError;
            if (!b.reg(d.address, 0xff, data, 1)) return;
            d.part = data[0];
            if (d.part != 0x11 && d.part != 0x15) { d.state = State::Unsupported; return; }
            if (!b.reg(d.address, 0xfe, data, 1)) return;
            d.revision = data[0]; d.driver = d.part == 0x11 ? "max30100" : "max30102"; d.state = State::Identified;
        } else if (d.address == 0x77) {
            d.state = State::IoError;
            if (!b.reg(d.address, 0xd0, data, 1)) return;
            d.part = data[0];
            if (d.part != 0x61) { d.state = State::Unsupported; return; }
            if (!b.reg(d.address, 0xf0, data, 1)) return;
            d.revision = data[0];
            if (d.revision > 1) { d.state = State::Unsupported; return; }
            d.driver = d.revision ? "bme688" : "bme680"; d.state = State::Identified;
        } else if (d.address == 0x5a) {
            d.state = State::IoError;
            const auto id = mlx90614::read_identity(b, d.address);
            if (id.status != mlx90614::Status::Valid) return;
            d.serial = id.serial; d.unique_serial = true;
            d.driver = "mlx90614-candidate"; d.state = State::Identified;
        } else if (d.address == 0x58) {
            // Transport implements the specified 10 ms command/read separation.
            d.state = State::IoError;
            const uint8_t feature[] = {0x20, 0x2f}, serial[] = {0x36, 0x82};
            if (!b.write(d.address, feature, 2) || !b.read(d.address, data, 3) || crc8(data, 2) != data[2]) return;
            d.part = uint16_t(data[0] << 8) | data[1];
            if ((d.part >> 12) != 0 || (d.part & 0xff) < 0x20) { d.state = State::Unsupported; return; }
            if (!b.write(d.address, serial, 2) || !b.read(d.address, data, 9)) return;
            for (int i = 0; i < 9; i += 3) if (crc8(data + i, 2) != data[i + 2]) return;
            for (int i = 0; i < 9; i += 3) d.serial = (d.serial << 16) | uint16_t(data[i] << 8) | data[i + 1];
            d.unique_serial = true; d.driver = "sgp30"; d.state = State::Identified;
        }
    }
};
} // namespace devices
