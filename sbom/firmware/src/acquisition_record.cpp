#include "acquisition_record.h"
#include "diagnostic_log.h"
#include "diagnostic_log_json.h"
#include "record_frame.h"
#include "device_acquisition.h"
#include "acquisition_config.h"
#include "process_bindings.h"
#include "source_snapshot.h"
#include "plc_core.h"
#include "modbus_tcp_server.h"
#include "config.h"
#include "plc_engine.h"
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <memory>
namespace {
char boot[33]{};
uint32_t sequence=0;
recording::Sender sender;
// Fixed output capacity, heap-backed once so it does not occupy the loop stack.
std::unique_ptr<char[]> wire;
void hex(const uint8_t* p,size_t n,char* out) {for(size_t i=0;i<n;++i)snprintf(out+2*i,3,"%02x",p[i]);}
String json(JsonDocument& d) {
    String text;text.reserve(measureJson(d)+1);
    struct Writer {String& text;size_t write(uint8_t c){text+=char(c);return 1;}
        size_t write(const uint8_t* p,size_t n){for(size_t i=0;i<n;++i)text+=char(p[i]);return n;}} writer{text};
    serializeJson(d,writer);return text;
}
void hash(const String& text,char* out){uint8_t h[32];mbedtls_sha256((const uint8_t*)text.c_str(),text.length(),h,0);hex(h,32,out);}
}
void acquisition_record_boot_begin(){
    if(!boot[0]){uint8_t id[16];esp_fill_random(id,sizeof(id));hex(id,sizeof(id),boot);}
}
const char* acquisition_record_boot_id(){return boot;}
bool acquisition_record_busy(){return sender.active();}
void acquisition_record_poll(){sender.poll(Serial,wire.get(),millis());}
bool acquisition_record_begin(const char* request,const char* index) {
    if(sender.active() || !recording::request_id(request) || !index || !*index || strlen(index)>2)return false;
    unsigned slot=0;for(const char* p=index;*p;++p){if(*p<'0'||*p>'9')return false;slot=slot*10+unsigned(*p-'0');}
    if((index[0]=='0'&&index[1]) || slot>=acquisition::Engine::Capacity)return false;
    const auto& engine=device_acquisition_state(); // loop owns this and all source I/O
    if(slot>=engine.count)return false;
    if(!wire){wire.reset(new(std::nothrow) char[recording::FrameMax]);if(!wire)return false;}
    if(!boot[0])return false;
    const auto source=engine.slots[slot];
    PlcSourceSnapshot snapshot;if(!plc_source_capture(snapshot))return false;
    uint64_t now64=snapshot.captured_us/1000;uint32_t now=uint32_t(now64);
    DynamicJsonDocument d(14336), program(4096), binding(6144);
    if(!d.capacity()||!program.capacity()||!binding.capacity())return false;
    d["schema"]="tbd.acquisition/v1";d["request_id"]=request;d["record_sequence"]=sequence;
    d["boot_id"]=boot;d["uptime_ms"]=now64;d["source_clock"]="boot_monotonic_read_completion";
    d["map_version"]=pointmap::VERSION;d["map_sha256"]=pointmap::SHA256;
    char elf[65]{};const auto* app=esp_app_get_description();if(!app)return false;hex(app->app_elf_sha256,32,elf);d["elf_sha256"]=elf;
    // Immutable program/config/mode copy from one lock interval. Serialization
    // and hashing happen after release, including this loop-owned raw path.
    String ir=plc_source_program_json(snapshot),text=plc_bindings_json(snapshot.bindings);
    if(!ir.length()||deserializeJson(program,ir.c_str())||deserializeJson(binding,text.c_str())||binding["bindings"]!="snapshot")return false;
    d["mode"]=snapshot.mode;d["faults"]=snapshot.faults;d["forces"]=snapshot.forces;
    d["bindings"]=binding.as<JsonObject>();
    char program_hash[65];hash(ir,program_hash);
    d["program"]=program.as<JsonObject>();d["program_sha256"]=program_hash;
    d["program_hash_basis"]="compact-ordered-json-magic-scan_ms-code-all-operands";
    d["entry_count"]=engine.count;d["entry_index"]=slot;d["epoch"]=engine.epoch;d["generation"]=engine.generation;
    d["enabled"]=engine.enabled;d["route_ok"]=engine.route_ok;d["overflow"]=engine.overflow;
    diagnostics::status_json(d.createNestedObject("diagnostics"),diagnostic_log_status());
    d["max_step_us"]=engine.max_step_us;d["max_batch_us"]=engine.max_batch_us;
    const auto& dev=source.device;auto id=d.createNestedObject("source");
    id["bus"]=dev.bus?"internal":"porta";id["mux"]=dev.mux;id["channel"]=dev.channel;id["address"]=dev.address;
    id["driver"]=dev.driver;id["part"]=dev.part;id["revision"]=dev.revision;id["unique_serial"]=dev.unique_serial;
    char serial[17];snprintf(serial,sizeof(serial),!strcmp(dev.driver,"mlx90614-candidate")?"%016llx":"%012llx",(unsigned long long)dev.serial);id["serial"]=serial;
    id["identity_basis"]=dev.unique_serial?"route_and_serial":"route_and_family";
    auto q=source.current(now,engine.enabled);bool valid=q==acquisition::Quality::Valid&&engine.route_ok&&!engine.overflow;
    d["quality"]=acquisition::name(q);d["valid"]=valid;d["has_sample"]=source.has_sample;
    d["sample_sequence"]=source.sequence;d["errors"]=source.errors;d["losses_min"]=source.losses;
    d["max_age_ms"]=source.max_age();d["calibration"]="provisional";
    if(source.has_sample){d["read_ms"]=source.sampled;d["age_ms"]=uint32_t(now-source.sampled);}
    else {d["read_ms"]=nullptr;d["age_ms"]=nullptr;}
    // Raw retained data never becomes valid through transport. Consumers must
    // use quality and per-field validity, not the existence of this object.
    auto values=d.createNestedObject("retained_values");const auto& v=source.value;
    if(source.has_sample){
        if(source.kind==acquisition::Kind::Env){
            values["temperature_c"]=v.temp_c;values["humidity_pct"]=v.rh_pct;values["pressure_pa"]=v.pressure_pa;
            values["sample_status"]=v.env_sample_status;values["sample_index"]=v.env_sample_index;
            values["gas_index"]=v.env_gas_index;values["res_heat"]=v.env_res_heat;values["idac"]=v.env_idac;values["gas_wait"]=v.env_gas_wait;
            values["gas_valid"]=v.gas_valid&&valid;
            if(v.gas_valid)values["gas_ohm"]=v.gas_ohm;else values["gas_ohm"]=nullptr;
        } else if(source.kind==acquisition::Kind::Sgp){values["eco2_estimate_ppm"]=v.eco2_ppm;values["tvoc_ppb"]=v.tvoc_ppb;values["humidity_compensated"]=false;values["baseline_restored"]=false;}
        else if(source.kind==acquisition::Kind::Heart){values["ir_raw"]=v.ir;values["red_raw"]=v.red;values["fifo_samples"]=v.fifo_samples;values["fifo_age_bound_ms"]=320;values["physiology_algorithm"]=false;}
        else if(source.kind==acquisition::Kind::Ncir){values["ambient_centi_c"]=v.ambient_centi_c;values["object_centi_c"]=v.object_centi_c;values["simultaneous_sample"]=false;}
        else {values["ch1_mv"]=v.mv1;values["ch1_ua"]=v.ua1;values["ch2_mv"]=v.mv2;values["ch2_ua"]=v.ua2;values["system_status"]=v.io_status;values["ch1_valid"]=valid&&!(v.io_status&1);values["ch2_valid"]=valid&&!(v.io_status&2);}
    }
    if(source.kind==acquisition::Kind::Env){
        auto config=d.createNestedObject("acquisition_config");config["id"]=acquisition::EnvConfigId;
        config["heater_c"]=acquisition::EnvHeaterC;config["heater_ms"]=acquisition::EnvHeaterMs;config["temperature_oversampling"]=8;config["humidity_oversampling"]=2;config["pressure_oversampling"]=4;config["filter_size"]=3;
        d["last_field_status"]=v.env_status;d["last_field_index"]=v.env_index;
    }
    if(d.overflowed()||program.overflowed()||binding.overflowed()||measureJson(d)>recording::PayloadMax)return false;
    String payload=json(d);size_t n=recording::frame(wire.get(),recording::FrameMax,request,sequence,payload.c_str(),payload.length());
    if(!n)return false;++sequence;return sender.start(n,millis());
}
