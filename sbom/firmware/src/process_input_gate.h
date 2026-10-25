#pragma once
#include <stdint.h>
#include <string.h>

// Immutable acquisition-owner publication consumed by the generated-controller
// owner. No hardware access, dynamic allocation or custom-VM dependency.
namespace process_input {
constexpr unsigned Count=3;
struct Sample {
    int32_t value=0;
    uint32_t sampled_ms=0,max_age_ms=0,epoch=0,generation=0,sequence=0;
    bool valid=false;
};
struct Snapshot {
    Sample samples[Count]{};
    uint32_t published_ms=0,config_generation=0;
    char config_sha256[65]{};
    bool config_ok=false;
};
class Gate {
    uint8_t mask_;
    const char* expected_;
    Snapshot admitted_{};
    bool armed_=false;
    static bool hash(const char* p) {
        if(!p)return false;
        for(unsigned i=0;i<64;++i)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return false;
        return p[64]==0;
    }
    bool identity(const Snapshot& s) const {
        if(s.config_generation!=admitted_.config_generation)return false;
        for(unsigned i=0;i<Count;++i)if(mask_&(1u<<i)) {
            const auto& a=s.samples[i];const auto& b=admitted_.samples[i];
            if(a.epoch!=b.epoch || a.generation!=b.generation || uint32_t(a.sequence-b.sequence)>INT32_MAX)return false;
        }
        return true;
    }
public:
    Gate(uint8_t mask,const char* expected):mask_(mask),expected_(expected){}
    bool ready(const Snapshot& s,uint32_t now) const {
        if(mask_&~7u)return false;
        if(!mask_)return true;
        if(!hash(expected_) || !hash(s.config_sha256) || strcmp(expected_,s.config_sha256) ||
           !s.config_ok || uint32_t(now-s.published_ms)>250)return false;
        for(unsigned i=0;i<Count;++i)if(mask_&(1u<<i)) {
            const auto& v=s.samples[i];
            if(!v.valid || !v.max_age_ms || v.max_age_ms>INT32_MAX || uint32_t(now-v.sampled_ms)>v.max_age_ms)return false;
        }
        return true;
    }
    bool arm(const Snapshot& s,uint32_t now) {
        armed_=false;
        if(!ready(s,now))return false;
        admitted_=s;armed_=true;return true;
    }
    void revoke(){armed_=false;}
    // Caller faults/inhibits outputs on refusal. A failed check revokes this
    // admission; a fresh explicit RUN admission is required even if data recovers.
    bool check(const Snapshot& current,uint32_t now,const Snapshot* consumed=nullptr) {
        bool ok=armed_ && ready(current,now) && (!mask_ || identity(current));
        if(ok && consumed && mask_) {
            ok=ready(*consumed,now) && consumed->config_generation==admitted_.config_generation;
            for(unsigned i=0;i<Count && ok;++i)if(mask_&(1u<<i)) {
                const auto& a=current.samples[i];const auto& b=consumed->samples[i];
                ok=a.epoch==b.epoch && a.generation==b.generation && uint32_t(a.sequence-b.sequence)<=INT32_MAX;
            }
        }
        if(!ok){revoke();return false;}
        admitted_=current;return true;
    }
};
}
