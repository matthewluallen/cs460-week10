// Explicit process roles over the existing acquisition engine. No bus operations.
#pragma once
#include "device_acquisition_core.h"
#include "plc_point_map.h"
#include "network_input_core.h"
#include <limits.h>
#include <stdio.h>

namespace bindings {
constexpr unsigned Count=3;
using Field=pointmap::ProcessField;
using FieldDef=pointmap::ProcessFieldDef;
static constexpr auto& Fields=pointmap::PROCESS_FIELDS;
struct Input {
    bool configured=false;
    bool network=false;
    network_input::Config remote;
    char role[25]{},driver[24]{};
    uint8_t bus=0,mux=0,address=0;
    int8_t channel=-1;
    uint16_t part=0,revision=0;
    bool unique_serial=false;
    uint64_t serial=0;
    Field field=Field::Temperature;
};
struct Config { uint8_t schema=1; Input inputs[Count]{}; };
inline bool identifier(const char* s,size_t cap) {
    size_t n=0;for(;n<cap && s[n];++n) if(!((s[n]>='a'&&s[n]<='z')||
        (s[n]>='0'&&s[n]<='9')||s[n]=='_'||s[n]=='-'))return false;
    return n && n<cap;
}
inline bool same_route(const Input& a,const Input& b) {
    return a.bus==b.bus&&a.mux==b.mux&&a.channel==b.channel&&a.address==b.address;
}
inline bool valid(const Config& c) {
    if(c.schema!=1&&c.schema!=2)return false;
    for(unsigned i=0;i<Count;++i) {const auto& a=c.inputs[i];if(!a.configured)continue;
        if(a.network) {
            if(c.schema!=2 || !identifier(a.role,sizeof(a.role)) || strcmp(a.driver,"modbus-sensor-v1") ||
                unsigned(a.field)>=unsigned(Field::Count) || a.remote.field!=unsigned(a.field) ||
                !network_input::valid(a.remote))return false;
            for(unsigned j=0;j<i;++j)if(c.inputs[j].configured&&!strcmp(a.role,c.inputs[j].role))return false;
            continue;
        }
        if(!identifier(a.role,sizeof(a.role))||!identifier(a.driver,sizeof(a.driver))||
            unsigned(a.field)>=unsigned(Field::Count)||a.bus>1||a.address<8||a.address>0x77)return false;
        if(a.mux) {if(a.bus||a.mux<0x70||a.mux>0x77||a.channel<0||a.channel>5||a.address==a.mux)return false;}
        else if(a.channel!=-1)return false;
        auto kind=acquisition::Kind(Fields[unsigned(a.field)].kind);
        bool env=!strcmp(a.driver,"bme680")||!strcmp(a.driver,"bme688");
        bool sgp=!strcmp(a.driver,"sgp30"),heart=!strcmp(a.driver,"max30100"),io=!strcmp(a.driver,"stamplc-io-candidate");
        bool ncir=!strcmp(a.driver,"mlx90614-candidate"),tof=!strcmp(a.driver,"vl53l1x-candidate");
        if(!(env||sgp||heart||io||ncir||tof) || (env&&kind!=acquisition::Kind::Env)||
            (sgp&&kind!=acquisition::Kind::Sgp)||(heart&&kind!=acquisition::Kind::Heart)||(io&&kind!=acquisition::Kind::Io)||(ncir&&kind!=acquisition::Kind::Ncir)||(tof&&kind!=acquisition::Kind::Tof))return false;
        if(env&&(a.bus||a.address!=0x77||a.part!=0x61||a.revision!=(!strcmp(a.driver,"bme688")?1:0)))return false;
        if(sgp&&(a.bus||a.address!=0x58||(a.part>>12)!=0||(a.part&0xff)<0x20||a.revision))return false;
        if(heart&&(a.bus||a.address!=0x57||a.part!=0x11||a.revision>255))return false;
        if(io&&(!a.bus||a.mux||a.address<0x20||a.address>0x2f||a.part!=a.address||a.revision>=255))return false;
        if(tof&&(a.bus||!a.mux||a.address!=0x29||a.part!=0xeacc||a.revision))return false;
        if(ncir&&(a.bus||a.address!=0x5a||a.part||a.revision))return false;
        if(a.unique_serial!=(sgp||ncir) || (sgp&&(a.serial>>48)) || (!(sgp||ncir)&&a.serial))return false;
        for(unsigned j=0;j<i;++j) {const auto& b=c.inputs[j];if(!b.configured)continue;
            if(!strcmp(a.role,b.role))return false;
            // One source may supply different fields, but cannot have conflicting identity declarations.
            if(!b.network && same_route(a,b) && (strcmp(a.driver,b.driver)||a.part!=b.part||a.revision!=b.revision||
                a.unique_serial!=b.unique_serial||a.serial!=b.serial||a.field==b.field))return false;
        }
    }return true;
}
enum class Quality : uint16_t { Unbound, Awaiting, Valid, Warmup, IoError, CrcError, Overflow, Stale, Paused, Missing, IdentityMismatch, RouteError, ValueInvalid, ConfigInvalid };
inline const char* name(Quality q) {
    const char* names[]={"unbound","awaiting","valid","warmup","io_error","crc_error","overflow","stale","paused","missing","identity_mismatch","route_error","value_invalid","config_invalid"};
    return unsigned(q)<sizeof(names)/sizeof(*names)?names[unsigned(q)]:"invalid";
}
struct Sample {
    int32_t value=0;
    Quality quality=Quality::Unbound;
    bool has_sample=false, has_value=false, unique_serial=false;
    uint32_t sampled=0, sequence=0, epoch=0, errors=0, losses=0, generation=0,max_age=0;
    bool usable(uint32_t now) const {return quality==Quality::Valid&&has_value&&now-sampled<=max_age;}
};
inline Sample resolve_network(const Input& a,const network_input::State& state,uint32_t generation) {
    Sample s;if(!a.configured||!a.network)return s;
    using N=network_input::Status;
    switch(state.status) {
    case N::Awaiting:s.quality=Quality::Awaiting;break;
    case N::Valid:s.quality=Quality::Valid;break;
    case N::IoError:s.quality=Quality::IoError;break;
    case N::ProtocolError:s.quality=Quality::IoError;break;
    case N::IdentityMismatch:s.quality=Quality::IdentityMismatch;break;
    case N::SourceInvalid:s.quality=Quality::ValueInvalid;break;
    case N::Replay:s.quality=Quality::Stale;break;
    case N::Stale:s.quality=Quality::Stale;break;
    case N::EpochChanged:s.quality=Quality::IdentityMismatch;break;
    }
    s.value=state.record.value;s.sampled=state.received_ms;s.sequence=state.record.sequence;
    s.epoch=state.record.epoch;s.generation=generation;s.errors=state.errors;
    s.has_sample=s.has_value=state.has_sample;s.max_age=a.remote.max_age_ms;
    return s;
}
inline bool convert(const acquisition::Values& v,Field field,int32_t& out) {
    double f=0;
    switch(field) {
    case Field::Distance:if(v.range_status!=9 || v.distance_mm<0 || v.distance_mm>65535)return false;out=v.distance_mm;return true;
    case Field::NcirAmbient:out=v.ambient_centi_c;return true;
    case Field::NcirObject:out=v.object_centi_c;return true;
    case Field::Temperature:f=double(v.temp_c)*10;break;
    case Field::Humidity:f=double(v.rh_pct)*1000;break;
    case Field::Pressure:f=v.pressure_pa;break;
    case Field::Gas:if(!v.gas_valid)return false;f=v.gas_ohm;break;
    case Field::Eco2:out=v.eco2_ppm;return true;case Field::Tvoc:out=v.tvoc_ppb;return true;
    case Field::Ir:out=v.ir;return true;case Field::Red:out=v.red;return true;
    case Field::Ch1Mv:if(v.io_status&1)return false;out=v.mv1;return true;case Field::Ch1Ua:if(v.io_status&1)return false;out=v.ua1;return true;
    case Field::Ch2Mv:if(v.io_status&2)return false;out=v.mv2;return true;case Field::Ch2Ua:if(v.io_status&2)return false;out=v.ua2;return true;
    default:return false;
    }
    if(!isfinite(f))return false;f=round(f);
    if(f<INT32_MIN||f>INT32_MAX)return false;out=int32_t(f);return true;
}
inline Sample resolve(const Input& a,const acquisition::Engine& e,uint32_t now) {
    Sample result;result.epoch=e.epoch;result.generation=e.generation;
    if(!a.configured)return result;
    if(unsigned(a.field)>=unsigned(Field::Count)){result.quality=Quality::ConfigInvalid;return result;}
    if(!e.route_ok||e.overflow||e.count>e.Capacity){result.quality=Quality::RouteError;return result;}
    const acquisition::Slot* slot=nullptr;unsigned matches=0;
    for(size_t i=0;i<e.count;++i) {const auto& d=e.slots[i].device;
        if(d.bus==a.bus&&d.mux==a.mux&&d.channel==a.channel&&d.address==a.address){slot=&e.slots[i];++matches;}
    }
    if(!matches){result.quality=e.enabled?Quality::Missing:Quality::Paused;return result;}
    if(matches!=1){result.quality=Quality::RouteError;return result;}
    const auto& s=*slot;const auto& d=s.device;
    if(d.state!=devices::State::Identified||strcmp(a.driver,d.driver)||a.part!=d.part||a.revision!=d.revision||
        a.unique_serial!=d.unique_serial||(a.unique_serial&&a.serial!=d.serial)||s.kind!=acquisition::Kind(Fields[unsigned(a.field)].kind)) {
        result.quality=Quality::IdentityMismatch;return result;
    }
    result.sampled=s.sampled;result.sequence=s.sequence;result.errors=s.errors;result.losses=s.losses;
    result.max_age=s.max_age();result.unique_serial=d.unique_serial;
    result.quality=s.current(now,e.enabled)==acquisition::Quality::SensorError?Quality::ValueInvalid:
        Quality(unsigned(s.current(now,e.enabled))+1); // acquisition order is part of this boundary
    result.has_sample=s.has_sample;
    result.has_value=s.has_sample&&convert(s.value,a.field,result.value);
    if(s.has_sample&&!result.has_value)result.quality=Quality::ValueInvalid;
    return result;
}
static_assert(unsigned(acquisition::Quality::Valid)==1 && unsigned(acquisition::Quality::Paused)==7,"update binding quality translation");
}
