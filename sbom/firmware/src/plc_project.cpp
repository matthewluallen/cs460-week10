#include "plc_project.h"
#include "plc_core.h"
#include "plc_tags.h"
#include "modbus_tcp_server.h"
#include "acquisition_record.h"
#include "process_bindings.h"
#include "io_expansion.h"
#include <ArduinoJson.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>

extern plc::Engine g_engine;
namespace {
project::Active active;
project::StartupRequest startup;
struct Guard { Guard(){plc_image_lock();} ~Guard(){plc_image_unlock();} };
struct ExpansionObservation {bool valid=false;uint8_t address=0,revision=0;uint32_t observed=0;} expansion;
bool temperature_valid=false;
uint32_t temperature_observed=0;
bool temperature_required() {
    for(auto source:active.definition.word_inputs)if(source==project::WordSource::BoardTemperature)return true;
    return false;
}
bool temperature_ready() {return !temperature_required()||(temperature_valid&&millis()-temperature_observed<=1000);}
bool expansion_ready() {
    return !active.definition.io_expansion || (expansion.valid && millis()-expansion.observed<=250 &&
        expansion.address==active.definition.io_address && expansion.revision==active.definition.io_revision);
}
const char* readiness_reason() {
    if(!active.ready())return active.status==project::Status::Invalid?"invalid_project":"no_project";
    if(!temperature_ready())return temperature_valid?"board_temperature_stale":"board_temperature_invalid";
    if(active.definition.io_expansion) {
        if(!ioexp_present())return "output_module_missing";
        if(!expansion.valid)return "output_module_invalid";
        if(millis()-expansion.observed>250)return "output_observation_stale";
        if(expansion.address!=active.definition.io_address||expansion.revision!=active.definition.io_revision)return "output_identity_mismatch";
    }
    if(const char* reason=plc_bindings_project_reason(project::process_mask(active.definition)))return reason;
    if(plc_fault_active_count())return "latched_fault";
    return nullptr;
}
}
const project::Active& plc_project_active() { return active; }
void plc_project_boot_status(project::Status status) {
    Guard guard; active = project::Active{}; active.status = status;
    startup = project::StartupRequest{};
    expansion={};temperature_valid=false;temperature_observed=0;
    plc_tags_clear();
}
bool plc_project_install(const project::Definition& candidate, const char* digest, bool saved, bool restored) {
    Guard guard;
    if (!active.commit(candidate, digest, saved)) return false;
    startup.reset(active.definition.startup, restored, millis());
    plc_bindings_activate_project(active.definition.process_bindings,active.generation);
    for(uint8_t i=0;i<project::HardwareOutputs;++i)modbus_server.set_coil(project::output_coil(i),false);
    plc_tags_install(active.definition);
    return true;
}
bool plc_project_ready() { Guard guard; return active.ready(); }
bool plc_project_can_run(bool arm) {
    Guard guard;return active.ready() && temperature_ready() && expansion_ready() && plc_bindings_project_ready(project::process_mask(active.definition),arm);
}
void plc_project_observe_temperature(bool valid) {Guard guard;temperature_valid=valid;temperature_observed=millis();}
void plc_project_refresh_expansion() {
    bool needed;
    {Guard guard;needed=active.ready()&&active.definition.io_expansion;}
    if(!needed)return;
    ExpansionObservation next;
    next.valid=ioexp_identity(&next.address,&next.revision);next.observed=millis();
    Guard guard;expansion=next;
}
uint32_t plc_project_generation() { Guard guard; return active.generation; }
void plc_project_inputs(const bool physical[plc::NDI], int32_t board_temperature) {
    if (active.ready()) {
        int32_t values[bindings::Count]{};
        const bool executing=plc_mode()!=PLC_PROGRAM;
        if(!plc_bindings_project_values(project::process_mask(active.definition),executing,values))
            plc_fault_raise(50,"project source invalid/epoch changed");
        if(executing&&!expansion_ready())plc_fault_raise(51,"project IO expansion identity/stale");
        if(executing&&!temperature_ready())plc_fault_raise(52,"project board temperature invalid");
        project::inputs(active.definition, g_engine, physical, board_temperature, active.parameters,values);
    }
    else {
        for (unsigned i = 0; i < plc::NDI; ++i) g_engine.set_input(i, false);
        for (unsigned i = 0; i < plc::NIW; ++i) g_engine.set_analog(i, 0);
    }
}
void plc_project_outputs(bool enabled, bool physical[project::HardwareOutputs]) {
    if (active.ready()) project::outputs(active.definition, g_engine, enabled, physical);
    else for (unsigned i = 0; i < project::HardwareOutputs; ++i) physical[i] = false;
}
bool plc_project_output_mapped(uint8_t physical) {
    if(active.ready())for(auto index:active.definition.digital_outputs)if(index==physical)return true;
    return false;
}
uint8_t plc_project_di(uint8_t logical) {
    Guard guard; return active.ready() && logical < plc::NDI ? active.definition.digital_inputs[logical] : logical;
}
uint8_t plc_project_qx(uint8_t logical) {
    Guard guard; return active.ready() && logical < plc::NQ ? active.definition.digital_outputs[logical] : logical;
}
int32_t plc_project_word(uint8_t logical) { Guard guard; return g_engine.analog(logical); }
bool plc_project_memory(uint8_t logical) { Guard guard; return g_engine.mem(logical); }
bool plc_project_set_parameter(uint8_t logical, int32_t value) {
    Guard guard;
    if (!active.ready() || logical >= plc::NIW || active.definition.word_inputs[logical] != project::WordSource::Parameter) return false;
    active.parameters[logical] = value; // live value only; project defaults persist on deployment
    return true;
}
bool plc_project_host_acl_enabled() { Guard guard; return active.ready() && active.definition.host_acl.enabled; }
bool plc_project_host_allowed(uint32_t ipv4) {
    Guard guard; return point_policy::host_allowed(active.definition.host_acl, ipv4);
}
bool plc_project_point_allowed(uint32_t ipv4, point_policy::Kind kind, uint16_t index, bool write) {
    Guard guard; return point_policy::allows(active.definition.host_acl, ipv4, kind, index, write);
}
bool plc_project_range_allowed(uint32_t ipv4, point_policy::Kind kind, uint16_t first, uint16_t count, bool write) {
    Guard guard; return point_policy::range_allowed(active.definition.host_acl, ipv4, kind, first, count, write);
}
bool plc_project_all_points_allowed(uint32_t ipv4, bool write) {
    Guard guard; return point_policy::all_points_allowed(active.definition.host_acl, ipv4, write);
}
void plc_project_startup_cancel(const char* reason) {
    Guard guard; startup.cancel(reason, millis());
}
void plc_project_startup_poll() {
#ifdef OPENPLC_GENERATED_CONTROLLER
    return; // Studio startup declarations are not native/generated controller code.
#endif
    Guard guard;
    if (startup.state != project::StartupState::Pending) return;
    if (plc_mode() != PLC_PROGRAM) { startup.cancel("mode_changed", millis()); return; }
    if (startup.consume(millis(), active.ready() && !readiness_reason(), plc_fault_active_count() != 0)) {
        // Consume first so this ordinary mode request cannot cancel itself or
        // be retried later after an admission failure or operator STOP.
        plc_set_mode(PLC_RUN, SRC_SYSTEM, "project startup");
        startup.finish(plc_mode() == PLC_RUN);
        plc_log(SRC_SYSTEM, "project startup", "%s", startup.reason);
    }
}
String plc_project_json() {
#ifdef OPENPLC_GENERATED_CONTROLLER
    return "{\"error\":\"compiled-controller profile does not load project-v1 applications\"}";
#endif
    Guard guard;
    // All callers hold the image lock; bounded shared scratch avoids adding ACL
    // readback capacity to either the loop or network task's stack.
    static StaticJsonDocument<6144> doc;
    doc.clear();
    doc["schema"] = "tbd.project-readback/v1";
    doc["installed"] = active.ready();
    doc["status"] = project::status_name(active.status);
    doc["project_id"] = active.ready() ? active.definition.id : "";
    doc["project_name"] = active.ready() ? active.definition.name : "";
    doc["project_revision"] = active.ready() ? active.definition.revision : 0;
    doc["payload_sha256"] = active.payload_sha256;
    doc["generation"] = active.generation;
    doc["scan_ms"] = project::ScanMs;
    doc["mode"] = plc_mode_str();
    auto startup_json = doc.createNestedObject("startup");
    startup_json["mode"] = project::startup_mode_name(active.definition.startup);
    startup_json["state"] = project::startup_state_name(startup.state);
    const char* startup_reason = startup.state == project::StartupState::Pending ? readiness_reason() : nullptr;
    startup_json["reason"] = startup_reason ? startup_reason : startup.reason;
    startup_json["timeout_ms"] = project::StartupTimeoutMs;
    startup_json["elapsed_ms"] = startup.elapsed(millis());
    auto acl_json = doc.createNestedObject("host_acl");
    acl_json["enabled"] = active.definition.host_acl.enabled;
    auto rules_json = acl_json.createNestedArray("rules");
    for (unsigned i = 0; i < active.definition.host_acl.count; ++i) {
        const auto& rule = active.definition.host_acl.rules[i];
        auto item = rules_json.createNestedObject(); char point[8], host[16];
        point_policy::format_point(rule.kind, rule.index, point, sizeof(point));
        point_policy::format_ipv4(rule.host, host, sizeof(host));
        item["point"] = point; item["host"] = host; item["access"] = point_policy::access_name(rule.access);
    }
#ifdef ENABLE_CWE_DEMOS
    doc["firmware_profile"] = "vulnerable";
    doc["runtime_profile"] = "vulnerable";
#else
    doc["firmware_profile"] = "secure";
    doc["runtime_profile"] = "secure";
#endif
    doc["runtime_abi"] = "tbd-project-v1-vm32";
    char elf_sha256[65];const auto* app=esp_app_get_description();
    for(unsigned i=0;i<32;++i)snprintf(elf_sha256+i*2,3,"%02x",app->app_elf_sha256[i]);
    doc["elf_sha256"]=elf_sha256;
    doc["modules_ready"]=plc_project_can_run(false);
    const char* reason=readiness_reason();
    doc["run_ready"]=reason==nullptr;
    doc["readiness_reason"]=reason?reason:"ready";
    doc["faults"]=plc_fault_active_count();
    doc["io_expansion_ready"]=active.ready()&&expansion_ready();
    if(active.ready()&&active.definition.io_expansion) {
        auto expected=doc.createNestedObject("io_expansion");
        expected["address"]=active.definition.io_address;expected["revision"]=active.definition.io_revision;
    } else doc["io_expansion"]=nullptr;
    PlcBindingsSnapshot sources;plc_bindings_capture(sources);
    doc["binding_config_sha256"]=sources.config_sha256;
    doc["boot_id"] = acquisition_record_boot_id();
    PlcScanSnapshot scan; plc_scan_capture(scan);
    const bool current = active.ready() && scan.available && scan.project_generation == active.generation && scan.mode_after == plc_mode();
    doc["sequence"] = current ? scan.sequence : 0;
    if (current) {
        auto logical = doc.createNestedObject("logical");
        auto ix = logical.createNestedArray("IX"), qx = logical.createNestedArray("QX");
        auto iw = logical.createNestedArray("IW"), mx = logical.createNestedArray("MX");
        for (unsigned i = 0; i < plc::NDI; ++i) ix.add(g_engine.input(i));
        for (unsigned i = 0; i < plc::NQ; ++i) qx.add(g_engine.relay(i));
        for (unsigned i = 0; i < plc::NIW; ++i) iw.add(g_engine.analog(i));
        for (unsigned i = 0; i < plc::NMX; ++i) mx.add(g_engine.mem(i));
    } else doc["logical"] = nullptr;
    if (doc.overflowed()) return "{\"error\":\"project readback capacity\"}";
    String out;
    struct Writer {String& out;size_t write(uint8_t c){out+=char(c);return 1;}
        size_t write(const uint8_t* p,size_t n){for(size_t i=0;i<n;++i)out+=char(p[i]);return n;}} writer{out};
    serializeJson(doc, writer); return out;
}
