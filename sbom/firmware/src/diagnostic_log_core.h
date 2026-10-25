// Fixed diagnostic retention; synchronization and clock acquisition belong to caller.
// Messages are retained as bytes. ASCII preview is a view, never replacement evidence.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace diagnostics {
enum class Origin : uint8_t { Sdk, Application, Bus, Ethernet, Mqtt, Telemetry, Selftest };
inline const char* name(Origin o) {
    switch(o) {
    case Origin::Sdk:return "sdk";case Origin::Application:return "application";
    case Origin::Bus:return "bus";case Origin::Ethernet:return "ethernet";
    case Origin::Mqtt:return "mqtt";case Origin::Telemetry:return "telemetry";
    case Origin::Selftest:return "selftest";
    }
    return "unknown";
}
inline void saturate(uint64_t& v) { if(v!=UINT64_MAX)++v; }
struct Entry {
    static constexpr size_t MaxBytes=192;
    uint64_t sequence=0,enqueue_us=0;
    uint32_t formatted_bytes=0;
    uint16_t retained_bytes=0;
    Origin origin=Origin::Sdk;
    bool truncated=false;
    uint8_t bytes[MaxBytes]{};
};
struct Status {
    uint64_t next_sequence=1,overwritten_min=0,truncated_min=0,format_errors_min=0,exhausted_min=0;
    uint64_t first_sequence=0,last_sequence=0;
    size_t retained=0;
};
class Ring {
public:
    static constexpr size_t Capacity=16;
private:
    Entry entries[Capacity]{};
    Status state;
    size_t next=0;
public:
    Status status() const {return state;}
    void format_error() {saturate(state.format_errors_min);}
    bool append(Origin origin, uint64_t now_us, const uint8_t* data,
                size_t available, uint32_t formatted_bytes) {
        // Never reuse an identifier at counter wrap. Reboot owns a new identity.
        if(state.next_sequence==UINT64_MAX){saturate(state.exhausted_min);return false;}
        if(!data && available){format_error();return false;}
        if(available>formatted_bytes){format_error();return false;}
        size_t keep=available<Entry::MaxBytes?available:Entry::MaxBytes;
        Entry value{};value.sequence=state.next_sequence++;value.enqueue_us=now_us;
        value.origin=origin;value.formatted_bytes=formatted_bytes;
        value.retained_bytes=uint16_t(keep);value.truncated=keep<formatted_bytes;
        if(keep)std::memcpy(value.bytes,data,keep);
        if(value.truncated)saturate(state.truncated_min);
        if(state.retained==Capacity)saturate(state.overwritten_min);else ++state.retained;
        entries[next]=value;next=(next+1)%Capacity;
        state.last_sequence=value.sequence;
        state.first_sequence=state.last_sequence-state.retained+1;
        return true;
    }
    bool read(uint64_t sequence, Entry& value) const {
        if(!state.retained || sequence<state.first_sequence || sequence>state.last_sequence)return false;
        const size_t oldest=(next+Capacity-state.retained)%Capacity;
        value=entries[(oldest+size_t(sequence-state.first_sequence))%Capacity];
        return value.sequence==sequence;
    }
};
inline void ascii_preview(const Entry& value,char (&out)[Entry::MaxBytes+1]) {
    for(size_t i=0;i<value.retained_bytes;++i)out[i]=value.bytes[i]>=32 && value.bytes[i]<=126?char(value.bytes[i]):'.';
    out[value.retained_bytes]=0;
}
inline void hexadecimal(const Entry& value,char (&out)[2*Entry::MaxBytes+1]) {
    constexpr char hex[]="0123456789abcdef";
    for(size_t i=0;i<value.retained_bytes;++i){out[2*i]=hex[value.bytes[i]>>4];out[2*i+1]=hex[value.bytes[i]&15];}
    out[2*value.retained_bytes]=0;
}
}
