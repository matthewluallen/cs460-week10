// Native Modbus/TCP sensor consumer. Pure code; no sockets, RTOS or hardware.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace network_input {
constexpr unsigned Words=20, ReplyBytes=9+2*Words;
struct Config {
    uint8_t host[4]{};
    uint16_t port=502, address=0, field=0;
    uint8_t unit=1;
    uint64_t device=0;
    uint32_t poll_ms=1000, timeout_ms=250, max_age_ms=3000;
};
inline bool valid(const Config& c) {
    return c.host[0]>0 && c.host[0]<224 && c.host[0]!=127 && c.host[3]!=255 &&
        c.port && c.unit>=1 && c.unit<=247 && c.device && c.address<=65536-Words &&
        c.poll_ms>=100 && c.poll_ms<=10000 && c.timeout_ms>=25 && c.timeout_ms<=1000 &&
        c.max_age_ms>=c.poll_ms && c.max_age_ms<=30000;
}
inline uint16_t u16(const uint8_t* p) {return uint16_t(p[0])<<8|p[1];}
inline uint32_t u32(const uint8_t* p) {return uint32_t(u16(p))<<16|u16(p+2);}
inline void put16(uint8_t* p,uint16_t v) {p[0]=uint8_t(v>>8);p[1]=uint8_t(v);}
inline void request(const Config& c,uint16_t transaction,uint8_t (&out)[12]) {
    put16(out,transaction);put16(out+2,0);put16(out+4,6);out[6]=c.unit;out[7]=3;
    put16(out+8,c.address);put16(out+10,Words);
}
enum class Status : uint8_t { Awaiting, Valid, IoError, ProtocolError, IdentityMismatch,
    SourceInvalid, Replay, Stale, EpochChanged };
inline const char* name(Status s) {
    const char* n[]={"awaiting","valid","io_error","protocol_error","identity_mismatch",
        "source_invalid","replay","stale","epoch_changed"};
    return unsigned(s)<sizeof(n)/sizeof(*n)?n[unsigned(s)]:"invalid";
}
struct Record {
    uint32_t epoch=0, sequence=0, source_ms=0, source_age_ms=0, source_limit_ms=0;
    int32_t value=0;
    uint16_t flags=0;
};
struct State {
    Status status=Status::Awaiting;
    Record record{};
    uint32_t received_ms=0, errors=0;
    bool has_sample=false, quarantined=false;
    bool usable(uint32_t now,const Config& c) const {
        return status==Status::Valid && has_sample && !quarantined && now-received_ms<=c.max_age_ms;
    }
    void fail(Status reason) {++errors;status=quarantined?Status::EpochChanged:reason;}
    bool accept(const Config& c,uint16_t transaction,const uint8_t* p,size_t n,uint32_t now) {
        if(quarantined){fail(Status::EpochChanged);return false;}
        if(n!=ReplyBytes || u16(p)!=transaction || u16(p+2)!=0 || u16(p+4)!=3+Words*2 ||
            p[6]!=c.unit || p[7]!=3 || p[8]!=Words*2) {fail(Status::ProtocolError);return false;}
        const uint8_t* w=p+9;
        if(u16(w)!=0x5350 || u16(w+2)!=1) {fail(Status::ProtocolError);return false;}
        uint64_t device=uint64_t(u32(w+4))<<32|u32(w+8);
        if(device!=c.device || u16(w+20)!=c.field) {fail(Status::IdentityMismatch);return false;}
        Record r;r.epoch=u32(w+12);r.sequence=u32(w+16);r.flags=u16(w+22);
        uint32_t bits=u32(w+24);
        // Defined two's-complement conversion without implementation-defined narrowing.
        r.value=bits<=INT32_MAX?int32_t(bits):-1-int32_t(UINT32_MAX-bits);
        r.source_ms=u32(w+28);r.source_age_ms=u32(w+32);r.source_limit_ms=u32(w+36);
        if(has_sample && r.epoch && r.epoch!=record.epoch) {
            quarantined=true;fail(Status::EpochChanged);return false;
        }
        if(!r.epoch || !r.sequence || r.flags>3 || !(r.flags&1) || !r.source_limit_ms ||
            r.source_limit_ms>30000 || r.source_age_ms>r.source_limit_ms) {fail(Status::SourceInvalid);return false;}
        if(has_sample && (r.sequence<record.sequence ||
            uint32_t(r.source_ms-record.source_ms)>INT32_MAX)) {fail(Status::Replay);return false;}
        if(has_sample && r.sequence==record.sequence) {
            if(r.value!=record.value || r.source_ms!=record.source_ms || r.flags!=record.flags ||
                r.source_limit_ms!=record.source_limit_ms || r.source_age_ms<record.source_age_ms) {
                fail(Status::Replay);return false;
            }
            // Even recovery from a transport error needs a new sample, not this duplicate.
            if(status==Status::Valid && now-received_ms>c.max_age_ms)status=Status::Stale;
            return false;
        }
        record=r;received_ms=now;has_sample=true;status=Status::Valid;return true;
    }
};
// Length-delimited TCP assembly. Reject oversize/extra bytes rather than truncating.
struct Frame {
    uint8_t bytes[ReplyBytes]{};
    size_t used=0, expected=0;
    bool invalid=false;
    bool feed(const uint8_t* p,size_t n) {
        if(invalid || n>sizeof(bytes)-used){invalid=true;return false;}
        memcpy(bytes+used,p,n);used+=n;
        if(used>=7) {
            expected=6+u16(bytes+4);
            if(u16(bytes+2)!=0 || expected<9 || expected>sizeof(bytes) || used>expected)invalid=true;
        }
        return !invalid && expected && used==expected;
    }
};
}
