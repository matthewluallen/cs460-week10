#include "process_bindings.h"
#include "process_bindings_json.h"
#include "acquisition_record.h"
#include "device_acquisition.h"
#include "network_input.h"
#include "plc_core.h"
#include "plc_project.h"
#include "modbus_tcp_server.h"
#include <Preferences.h>
#include <mbedtls/sha256.h>
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
extern void plc_publication_inputs_begin();
extern void plc_publication_inputs_end();
struct PublicationInputs {
    PublicationInputs(){plc_publication_inputs_begin();}
    ~PublicationInputs(){plc_publication_inputs_end();}
};
#endif

namespace {
struct Guard {Guard(){plc_image_lock();}~Guard(){plc_image_unlock();}};
bindings::Config config;
bindings::Sample samples[bindings::Count];
network_input::State network_samples[bindings::Count];
uint32_t armed_epoch[bindings::Count]{},armed_generation[bindings::Count]{},last_sequence[bindings::Count]{};
uint32_t config_generation=0,published_ms=0;
uint8_t status=0,digest[32]{};
bindings::ProjectGate project_gate;
char hash_text[65]{};
// Buffers used only by the loop owner's PROGRAM configuration path.
char canonical[bindings::JsonCapacity]{};
void configure_network() {
    for(unsigned i=0;i<bindings::Count;++i) {
        const auto& a=config.inputs[i];network_input_configure(i,a.configured&&a.network,a.remote);
    }
}
void hash(const char* text,size_t n) {
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(text),n,digest,0);
    for(unsigned i=0;i<32;++i)snprintf(hash_text+i*2,3,"%02x",digest[i]);
}
uint8_t dependencies(const plc::Instr* p,int n) {
    if(plc::validate_program(p,n))return 0xff;
    uint8_t mask=0;
    for(int i=0;i<n;++i){if(p[i].op==plc::OP_HALT)break;
        if(p[i].op==plc::OP_LD_IW)mask|=uint8_t(1u<<p[i].a);}
    return mask & 0x8e; // IW1,2,3 and unbound IW7
}
void word32(unsigned a,uint32_t v) {
    modbus_server.set_register(a,uint16_t(v>>16));modbus_server.set_register(a+1,uint16_t(v));
}
void publish(uint32_t now) {
    word32(pointmap::BINDING_CLOCK_REGISTER,published_ms);
    word32(pointmap::BINDING_CONFIG_GENERATION_REGISTER,config_generation);
    modbus_server.set_register(pointmap::BINDING_FORMAT_REGISTER,1);
    unsigned count=0;
    for(unsigned i=0;i<bindings::Count;++i){const auto& s=samples[i];count+=config.inputs[i].configured;
        unsigned a=pointmap::BINDING_SLOT_BASE+i*pointmap::BINDING_SLOT_WORDS;
        auto quality=s.quality;if(quality==bindings::Quality::Valid&&!s.usable(now))quality=bindings::Quality::Stale;
        word32(a,uint32_t(s.value));modbus_server.set_register(a+2,unsigned(quality));
        modbus_server.set_register(a+3,(s.usable(now)?1:0)|(s.has_sample?2:0)|(s.has_value?4:0)|(s.unique_serial?8:0));
        word32(a+4,s.sampled);word32(a+6,s.sequence);word32(a+8,s.epoch);
        word32(a+10,s.errors);word32(a+12,s.losses);word32(a+14,s.generation);
    }
    modbus_server.set_register(pointmap::BINDING_STATUS_REGISTER,status);
    modbus_server.set_register(pointmap::BINDING_COUNT_REGISTER,count);
    for(unsigned i=0;i<16;++i)modbus_server.set_register(pointmap::BINDING_DIGEST_START+i,uint16_t(digest[2*i])<<8|digest[2*i+1]);
}
}
void plc_bindings_begin() {
    Guard guard;
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    PublicationInputs publication;
#endif
    config=bindings::Config{};status=0;config_generation=0;
    #ifdef OPENPLC_GENERATED_CONTROLLER
    Preferences pref;
    // Create an empty namespace at first boot; inability to open is visible.
    if(!pref.begin("bindings",false))status=1;
    else {
        String text=pref.getString("config","");pref.end();
        if(text.length() && !bindings::decode(text.c_str(),text.length(),config))status=2;
    }
    #else
    // A generic project never inherits an independently saved application binding.
    if(plc_project_ready()){config=plc_project_active().definition.process_bindings;config_generation=plc_project_generation();}
    #endif
    project_gate={};
    size_t n=bindings::encode(config,canonical,sizeof(canonical));hash(canonical,n);
    for(auto& s:samples)s={};configure_network();plc_bindings_refresh();
}
void plc_bindings_activate_project(const bindings::Config& candidate,uint32_t generation) {
    Guard guard;
    config=candidate;config_generation=generation;status=0;project_gate={};
    const size_t n=bindings::encode(config,canonical,sizeof(canonical));
    if(!n)status=2;
    hash(canonical,n);
    for(auto& s:samples)s={};
    configure_network();plc_bindings_refresh();
}
bool plc_bindings_project_ready(uint8_t mask,bool arm) {
    Guard guard;return project_gate.ready(mask,samples,status,millis(),arm);
}
const char* plc_bindings_project_reason(uint8_t mask) {
    Guard guard;
    if(mask&&status)return "binding_config_invalid";
    for(unsigned i=0;i<bindings::Count;++i)if(mask&(1u<<i)) {
        const auto& s=samples[i];
        if(s.usable(millis()))continue;
        switch(s.quality) {
        case bindings::Quality::Unbound:case bindings::Quality::Awaiting:case bindings::Quality::Missing:case bindings::Quality::Paused:
            return "missing_or_uncommissioned_source";
        case bindings::Quality::IdentityMismatch:return "source_identity_mismatch";
        case bindings::Quality::Stale:case bindings::Quality::Valid:return "stale_source";
        case bindings::Quality::Warmup:return "source_warming_up";
        default:return "invalid_source";
        }
    }
    return nullptr;
}
bool plc_bindings_project_values(uint8_t mask,bool executing,int32_t (&values)[bindings::Count]) {
    Guard guard;const uint32_t now=millis();
    for(unsigned i=0;i<bindings::Count;++i)values[i]=!status&&samples[i].usable(now)?samples[i].value:0;
    return !executing||project_gate.check(mask,samples,status,now);
}
void plc_bindings_refresh() {
    // Main loop is the sole acquisition writer. Publish a small resolved copy;
    // protocol/HTTP workers never inspect its mutable device state directly.
    bindings::Sample next[bindings::Count];const auto& e=device_acquisition_state();uint32_t now=millis();
    Guard guard;
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    PublicationInputs publication;
#endif
    for(unsigned i=0;i<bindings::Count;++i) {
        const auto& a=config.inputs[i];
        network_samples[i]=network_input_snapshot(i);
        next[i]=a.network?bindings::resolve_network(a,network_samples[i],config_generation):bindings::resolve(a,e,now);
        if(status)next[i].quality=bindings::Quality::ConfigInvalid;
        samples[i]=next[i];
    }
    published_ms=now;publish(now);
}
void plc_bindings_invalidate() {
    Guard guard;
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    PublicationInputs publication;
#endif
    for(auto& s:samples)if(s.quality!=bindings::Quality::Unbound)s.quality=bindings::Quality::Awaiting;
    published_ms=millis();publish(published_ms);
}
bool plc_bindings_ready(const plc::Instr* program,int count,bool arm) {
    Guard guard;uint8_t mask=dependencies(program,count);uint32_t now=millis();
    if(mask&0x80)return false;
    for(unsigned i=0;i<bindings::Count;++i)if(mask&(1u<<(i+1))) {
        if(status||!samples[i].usable(now))return false;
    }
    if(arm)for(unsigned i=0;i<bindings::Count;++i){armed_epoch[i]=samples[i].epoch;
        armed_generation[i]=samples[i].generation;last_sequence[i]=samples[i].sequence;}
    return true;
}
void plc_bindings_scan(plc::Engine& engine,const plc::Instr* program,int count,bool executing) {
    Guard guard;const uint8_t mask=dependencies(program,count);uint32_t now=millis();bool invalid=(mask&0x80)!=0;
    for(unsigned i=0;i<bindings::Count;++i) {
        const auto& s=samples[i];bool ready=!status&&s.usable(now);
        engine.set_analog(i+1,ready?s.value:0); // invalid words are never executed
        if(mask&(1u<<(i+1))) {
            if(!ready||s.epoch!=armed_epoch[i]||s.generation!=armed_generation[i]||
                int32_t(s.sequence-last_sequence[i])<0)invalid=true;
        }
        last_sequence[i]=s.sequence;
    }
    if(executing&&invalid)plc_fault_raise(50,"process input invalid or epoch changed");
}
bool plc_bindings_load(const char* json,size_t size,const PlcBindingVersion* expected) {
    bindings::Config staged;if(!bindings::decode(json,size,staged))return false;
    Guard guard;if(plc_mode()!=PLC_PROGRAM||config_generation==UINT32_MAX)return false;
#ifndef OPENPLC_GENERATED_CONTROLLER
    if(plc_project_ready())return false; // edit and redeploy the complete project
#endif
    if(expected&&(!acquisition_record_boot_id()[0]||expected->boot_id[32]||expected->config_sha256[64]||
        expected->generation!=config_generation||memcmp(expected->boot_id,acquisition_record_boot_id(),32)||
        memcmp(expected->config_sha256,hash_text,64)))return false;
    size_t n=bindings::encode(staged,canonical,sizeof(canonical));if(!n)return false;
    Preferences pref;if(!pref.begin("bindings",false))return false;
    bool saved=pref.putString("config",canonical)==n;
    String readback=pref.getString("config","");pref.end();
    if(!saved||readback.length()!=n||memcmp(readback.c_str(),canonical,n))return false;
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    PublicationInputs publication;
#endif
    config=staged;status=0;++config_generation;hash(canonical,n);
    configure_network();
    plc_bindings_refresh();plc_log(SRC_USB,"","process bindings saved generation=%lu",(unsigned long)config_generation);
    return true;
}
void plc_bindings_capture(PlcBindingsSnapshot& out) {
    Guard guard;
    out.config=config;memcpy(out.samples,samples,sizeof(samples));
    memcpy(out.network,network_samples,sizeof(network_samples));
    out.generation=config_generation;out.published_ms=published_ms;
    out.observed_ms=millis();out.status=status;
    memcpy(out.boot_id,acquisition_record_boot_id(),33);
    memcpy(out.config_sha256,hash_text,sizeof(hash_text));
}
String plc_bindings_json(const PlcBindingsSnapshot& snapshot) {
    const auto& config=snapshot.config;const auto& samples=snapshot.samples;
    const auto status=snapshot.status;const uint32_t now=snapshot.observed_ms;
    DynamicJsonDocument d(6144);
    d["bindings"]="snapshot";d["schema"]=1;d["map_version"]=pointmap::VERSION;d["map_sha256"]=pointmap::SHA256;
    d["boot_id"]=snapshot.boot_id;
    d["config_sha256"]=snapshot.config_sha256;d["config_generation"]=snapshot.generation;d["status"]=status;
    d["published_ms"]=snapshot.published_ms;d["observed_ms"]=now;d["clock"]="boot_monotonic_read_completion";
    bindings::write_config(d.createNestedObject("config"),config);
    auto entries=d.createNestedArray("inputs");
    for(unsigned i=0;i<bindings::Count;++i) {
        const auto& a=config.inputs[i];const auto& s=samples[i];auto v=entries.createNestedObject();
        v["iw"]=i+1;v["role"]=a.configured?a.role:"";v["valid"]=s.usable(now)&&!status;
        auto q=s.quality;if(q==bindings::Quality::Valid&&!s.usable(now))q=bindings::Quality::Stale;
        v["quality"]=bindings::name(q);v["has_sample"]=s.has_sample;
        if(s.usable(now)&&!status)v["value"]=s.value;else v["value"]=nullptr;
        if(s.has_value)v["retained_value"]=s.value;else v["retained_value"]=nullptr;
        if(s.has_sample){v["read_ms"]=s.sampled;v["age_ms"]=uint32_t(now-s.sampled);}else {v["read_ms"]=nullptr;v["age_ms"]=nullptr;}
        v["sequence"]=s.sequence;v["epoch"]=s.epoch;v["generation"]=s.generation;v["errors"]=s.errors;v["losses_min"]=s.losses;
        v["max_age_ms"]=s.max_age;v["calibration"]="provisional";
        v["identity_basis"]=s.unique_serial?"route_and_serial":"route_and_family";
        if(a.network) {
            const auto& n=snapshot.network[i];
            v["identity_basis"]="configured_endpoint_and_reported_device_id";
            auto detail=v.createNestedObject("network");detail["profile"]="modbus-sensor-v1";
            detail["status"]=network_input::name(n.status);
            detail["source_sample_ms"]=n.record.source_ms;detail["source_age_ms"]=n.record.source_age_ms;
            detail["source_limit_ms"]=n.record.source_limit_ms;detail["modeled"]=bool(n.record.flags&2);
            detail["quarantined"]=n.quarantined;detail["authentication"]="none";
            detail["source_clock"]="producer_boot_monotonic";
        }
        if(a.configured){const auto& f=bindings::Fields[unsigned(a.field)];v["field"]=f.name;v["unit"]=f.unit;v["scale"]=f.scale;}
    }
    String text;if(d.overflowed())return String("{\"bindings\":\"error\",\"reason\":\"snapshot capacity\"}");
    text.reserve(4096);
    struct Writer {String& text;size_t write(uint8_t c){text+=char(c);return 1;}
        size_t write(const uint8_t* p,size_t n){for(size_t i=0;i<n;++i)text+=char(p[i]);return n;}} writer{text};
    serializeJson(d,writer);return text;
}
String plc_bindings_json() {
    PlcBindingsSnapshot snapshot;plc_bindings_capture(snapshot);
    return plc_bindings_json(snapshot);
}
void plc_bindings_console(const char* arg) {
    while(*arg==' '||*arg=='\t')++arg;
    if(!*arg||!strcmp(arg,"json")) {String text=plc_bindings_json();
        // Console delivery has a bounded deadline and handles partial USB writes.
        uint32_t start=millis();size_t pos=0;
        while(pos<text.length()&&millis()-start<750){size_t n=text.length()-pos;if(n>32)n=32;
            if(Serial.availableForWrite()<int(n)){delay(1);continue;}pos+=Serial.write(reinterpret_cast<const uint8_t*>(text.c_str()+pos),n);}
        Serial.println();return;
    }
    if(!strncmp(arg,"apply ",6)) {
        // Fixed-width hex tokens and bounded decimal generation, then normal config codec.
        const char* p=arg+6;PlcBindingVersion expected;bool valid=true;
        auto hex_token=[&](char* out,unsigned count) {
            for(unsigned i=0;i<count;++i){char c=*p;if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;out[i]=c;++p;}
            if(*p!=' ')return false;++p;return true;
        };
        valid=hex_token(expected.boot_id,32);
        uint64_t generation=0;unsigned digits=0;
        if(valid){while(*p>='0'&&*p<='9'){if(++digits>10){valid=false;break;}generation=generation*10+unsigned(*p++-'0');}
            if(!digits||generation>UINT32_MAX||*p!=' ')valid=false;else ++p;}
        if(valid)valid=hex_token(expected.config_sha256,64);
        expected.generation=uint32_t(generation);
        bool ok=valid&&plc_bindings_load(p,strlen(p),&expected);
        Serial.println(ok?"{\"bindings\":\"saved\"}":"{\"bindings\":\"error\",\"reason\":\"invalid or stale assignment, non-PROGRAM mode or persistence failure\"}");return;
    }
    if(!strncmp(arg,"load ",5)) {
        bool ok=plc_bindings_load(arg+5,strlen(arg+5));
        Serial.println(ok?"{\"bindings\":\"saved\"}":"{\"bindings\":\"error\",\"reason\":\"invalid config, non-PROGRAM mode or persistence failure\"}");return;
    }
    Serial.println("{\"bindings\":\"error\",\"reason\":\"use bindings json, bindings apply <boot> <generation> <hash> <json> or bindings load <json>\"}");
}

void plc_bindings_process_capture(process_input::Snapshot& out) {
    Guard guard;out={};out.config_ok=status==0;
    out.config_generation=config_generation;out.published_ms=published_ms;
    memcpy(out.config_sha256,hash_text,sizeof(hash_text));
    static_assert(bindings::Count==process_input::Count,"process input count mismatch");
    for(unsigned i=0;i<bindings::Count;++i) {
        const auto& s=samples[i];auto& v=out.samples[i];
        v.value=s.value;v.sampled_ms=s.sampled;v.max_age_ms=s.max_age;
        v.epoch=s.epoch;v.generation=s.generation;v.sequence=s.sequence;
        v.valid=config.inputs[i].configured && s.has_sample && s.has_value && s.quality==bindings::Quality::Valid;
    }
}
