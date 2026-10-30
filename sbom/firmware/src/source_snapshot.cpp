#include "source_snapshot.h"
#include "acquisition_record.h"
#include "loop_timing.h"
#include "plc_core.h"
#include "modbus_tcp_server.h"
#include "config.h"
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
extern plc::Instr g_prog[32];
extern int g_prog_n;
namespace {
struct Guard {Guard(){plc_image_lock();}~Guard(){plc_image_unlock();}};
String json(JsonDocument& d) {
    String text;text.reserve(measureJson(d)+1);
    struct Writer {String& text;size_t write(uint8_t c){text+=char(c);return 1;}
        size_t write(const uint8_t* p,size_t n){for(size_t i=0;i<n;++i)text+=char(p[i]);return n;}} writer{text};
    serializeJson(d,writer);return text;
}
void hex(const uint8_t* p,char* out){for(unsigned i=0;i<32;++i)snprintf(out+2*i,3,"%02x",p[i]);}
void metric(JsonObject out,const timing::Metric& value) {
    out["count"]=value.count;out["saturated"]=value.saturated;
    if(value.count){out["last_us"]=value.last_us;out["min_us"]=value.min_us;out["max_us"]=value.max_us;}
    else {out["last_us"]=nullptr;out["min_us"]=nullptr;out["max_us"]=nullptr;}
    out["over_50ms"]=value.over_50ms;
}
template<class T> void execution_metric(JsonObject out,const T& value) {
    out["count"]=value.count;out["saturated"]=value.saturated;
    if(value.count){out["last_us"]=value.last_us;out["min_us"]=value.min_us;out["max_us"]=value.max_us;}
    else {out["last_us"]=nullptr;out["min_us"]=nullptr;out["max_us"]=nullptr;}
}
const char* mode_name(PlcMode value) {return value==PLC_RUN ? "RUN" : value==PLC_REMOTE ? "REMOTE" : "PROGRAM";}
}
bool plc_source_capture(PlcSourceSnapshot& out) {
#ifdef OPENPLC_GENERATED_CONTROLLER
    return false; // Old IR/source schema cannot represent the compiled program.
#endif
    Guard guard;
    if(g_prog_n<0||g_prog_n>32)return false;
    out.program_count=g_prog_n;memcpy(out.program,g_prog,sizeof(g_prog));
    plc_bindings_capture(out.bindings);
    plc_scan_capture(out.last_scan);
    snprintf(out.mode,sizeof(out.mode),"%s",plc_mode_str());
    out.faults=plc_fault_active_count();out.forces=plc_force_count();
    out.captured_us=esp_timer_get_time();return true;
}
String plc_source_program_json(const PlcSourceSnapshot& snapshot) {
    if(snapshot.program_count<0||snapshot.program_count>32)return String();
    DynamicJsonDocument d(4096);d["magic"]="TBD-IR";d["scan_ms"]=OPENPLC_SCAN_CYCLE_MS;
    auto code=d.createNestedArray("code");
    for(int i=0;i<snapshot.program_count;++i){auto row=code.createNestedArray();
        row.add(snapshot.program[i].op);row.add(snapshot.program[i].a);row.add(snapshot.program[i].b);}
    return d.overflowed()?String():json(d);
}
String plc_source_json(const PlcSourceSnapshot& snapshot) {
    const auto time=plc_timing_snapshot(); // independent timing publication, own captured_us
    const auto* app=esp_app_get_description();
    if(!app||!acquisition_record_boot_id()[0])return String("{\"error\":\"source identity\"}");
    String program=plc_source_program_json(snapshot),binding=plc_bindings_json(snapshot.bindings);
    if(!program.length())return String("{\"error\":\"program capacity\"}");
    DynamicJsonDocument d(8192),b(6144);
    if(deserializeJson(b,binding.c_str())||b["bindings"]!="snapshot")return String("{\"error\":\"binding capacity\"}");
    d["schema"]="tbd.binding-readback/v1";d["boot_id"]=acquisition_record_boot_id();
    char elf[65],sha[65];uint8_t hash[32];hex(app->app_elf_sha256,elf);
    mbedtls_sha256(reinterpret_cast<const uint8_t*>(program.c_str()),program.length(),hash,0);hex(hash,sha);
    d["elf_sha256"]=elf;d["program_sha256"]=sha;
    d["program_hash_basis"]="compact-ordered-json-magic-scan_ms-code-all-operands";
    d["captured_us"]=snapshot.captured_us;d["clock"]="esp_timer_boot_us";
    d["mode"]=snapshot.mode;d["faults"]=snapshot.faults;d["forces"]=snapshot.forces;
    d["bindings"]=b.as<JsonObject>();
    auto t=d.createNestedObject("timing");t["schema"]="tbd.loop-timing/v1";
    t["clock"]="esp_timer_boot_us";t["captured_us"]=time.captured_us;
    t["clock_errors"]=time.clock_errors;t["loop_pending"]=time.loop_pending;t["scan_pending"]=time.scan_pending;
    t["scan_target_us"]=uint64_t(OPENPLC_SCAN_CYCLE_MS)*1000;
    metric(t.createNestedObject("loop_busy"),time.loop_busy);metric(t.createNestedObject("loop_period"),time.loop_period);
    metric(t.createNestedObject("scan_busy"),time.scan_busy);metric(t.createNestedObject("scan_period"),time.scan_period);
    if(d.overflowed()||measureJson(d)>6144)return String("{\"error\":\"source capacity\"}");
    return json(d);
}
String plc_source_json() {
#ifdef OPENPLC_GENERATED_CONTROLLER
    return plc_generated_json();
#endif
    PlcSourceSnapshot snapshot;
    if(!plc_source_capture(snapshot))return String("{\"error\":\"source capture\"}");
    return plc_source_json(snapshot);
}

String plc_scan_json(const PlcSourceSnapshot& snapshot) {
    DynamicJsonDocument d(3072);
    d["schema"]="controller-scan-readback/v1";
    d["boot_id"]=acquisition_record_boot_id();
    d["captured_us"]=snapshot.captured_us;
    d["replay_complete"]=false;
    d["runtime_backend"]="custom-vm";
    if(snapshot.last_scan.available) {
        const auto& s=snapshot.last_scan;
        PlcSourceSnapshot scan_program;scan_program.program_count=s.program_count;
        memcpy(scan_program.program,s.program,sizeof(s.program));
        String scan_ir=plc_source_program_json(scan_program);
        if(!scan_ir.length())return String("{\"error\":\"scan program capacity\"}");
        uint8_t digest[32];char scan_hash[65];
        mbedtls_sha256(reinterpret_cast<const uint8_t*>(scan_ir.c_str()),scan_ir.length(),digest,0);hex(digest,scan_hash);
        auto scan=d.createNestedObject("last_scan");scan["schema"]="controller-scan/v1";
        scan["sequence"]=s.sequence;scan["saturated"]=s.saturated;
        scan["started_us"]=s.started_us;scan["completed_us"]=s.completed_us;
        scan["clock"]="esp_timer_boot_us";scan["program_sha256"]=scan_hash;
        auto phases=scan.createNestedObject("phase_end_us");
        static const char* names[]={"acquisition","binding_refresh","input_and_evidence","solve","outputs_and_expansion_readback","base_readback","status_and_fault_handling"};
        for(unsigned i=0;i<7;++i)phases[names[i]]=s.phase_end_us[i];
        scan["mode_at_solve"]=unsigned(s.mode_at_solve);scan["mode_after"]=unsigned(s.mode_after);
        scan["solve_attempted"]=s.solve_attempted;scan["solve_ok"]=s.solve_ok;
        scan["raw_di_mask"]=s.raw_di;scan["input_di_mask"]=s.input_di;
        scan["di_force_mask"]=s.di_force_mask;scan["coil_force_mask"]=s.coil_force_mask;
        auto iw=scan.createNestedArray("analog");for(auto v:s.analog)iw.add(v);
        scan["logic_output_mask"]=s.logic_outputs;scan["final_command_mask"]=s.applied_outputs;
        scan["register_readback_mask"]=s.register_readback;
        scan["binding_generation"]=s.binding_generation;
        auto sequences=scan.createNestedArray("sample_sequences");for(auto v:s.sample_sequence)sequences.add(v);
        auto times=scan.createNestedArray("sample_ms");for(auto v:s.sample_ms)times.add(v);
        scan["evidence_kind"]="controller-report-not-independent-bus-or-contact-observation";
    } else d["last_scan"]=nullptr;

    if(d.overflowed()||measureJson(d)>2048)return String("{\"error\":\"scan capacity\"}");
    return json(d);
}
String plc_scan_json() {
#ifdef OPENPLC_GENERATED_CONTROLLER
    return plc_generated_json();
#endif
    PlcSourceSnapshot snapshot;
    if(!plc_source_capture(snapshot))return String("{\"error\":\"source capture\"}");
    return plc_scan_json(snapshot);
}

String plc_loop_phases_json() {
    const auto time=plc_timing_snapshot();
    DynamicJsonDocument d(4096);
    d["schema"]="controller-loop-phases/v1";d["boot_id"]=acquisition_record_boot_id();
    d["clock"]="esp_timer_boot_us";d["captured_us"]=time.captured_us;
    d["clock_errors"]=time.clock_errors;d["loop_pending"]=time.loop_pending;
    d["scope"]="elapsed-phase-time-includes-waits-and-preemption-not-exclusive-cpu-cost";
    static const char* names[]={"front_services","acquisition","scan_slot","network_services","telemetry","display"};
    auto phases=d.createNestedObject("phases");
    for(unsigned i=0;i<timing::PHASE_COUNT;++i)metric(phases.createNestedObject(names[i]),time.phases[i]);
    static const char* panel_names[]={"button_update","boot_render","normal_render"};
    auto panel=d.createNestedObject("panel_operations");
    for(unsigned i=0;i<timing::PANEL_OPERATION_COUNT;++i)metric(panel.createNestedObject(panel_names[i]),time.panel[i]);
    if(d.overflowed()||measureJson(d)>3072)return String("{\"error\":\"loop phase capacity\"}");
    return json(d);
}

String plc_execution_json() {
    PlcExecutionSnapshot value;plc_execution_capture(value);
    return plc_execution_json(value);
}
String plc_execution_json(const PlcExecutionSnapshot& value) {
    // Capture is complete. Serialization neither samples hardware nor changes
    // the mode/history; callers may separately retain a network admission lock.
    DynamicJsonDocument d(8192);
    d["schema"]="tbd.execution-readback/v1";
    d["boot_id"]=acquisition_record_boot_id();d["clock"]="esp_timer_boot_us";
    d["captured_us"]=value.captured_us;d["supported"]=value.supported;
    d["runtime_backend"]=value.supported ? "custom-vm" : "generated-controller";
#ifdef ENABLE_CWE_DEMOS
    d["firmware_profile"]="vulnerable";
#else
    d["firmware_profile"]="secure";
#endif
    d["project_generation"]=value.project_generation;d["mode"]=mode_name(value.mode);
    d["active_faults"]=value.active_faults;
    auto policy=d.createNestedObject("policy");
    policy["period_us"]=execution::PeriodUs;
    policy["scan_fault_threshold_us"]=execution::ScanFaultUs;
    policy["scan_fault_comparison"]=">";
    policy["scan_fault_scope"]="entry_through_base_readback";
    policy["scan_fault_action"]="PROGRAM_and_same_scan_output_inhibition";
    policy["deadline_miss_action"]="observe_only";
    policy["scheduler"]="actual_start_anchor_no_catch_up";
    auto timing=d.createNestedObject("timing");const auto& t=value.timing;
    timing["elapsed_scope"]="entry_through_completed_evidence_including_waits";
    timing["completed_scans"]=t.completed;timing["scheduled_scans"]=t.scheduled;timing["maintenance_scans"]=t.maintenance;
    timing["saturated"]=t.saturated;timing["clock_errors"]=t.clock_errors;
    timing["scheduled_late_releases"]=t.late_releases;timing["scheduled_missed_releases"]=t.missed_releases;
    timing["scheduled_budget_misses"]=t.scheduled_budget_misses;
    timing["consecutive_budget_misses"]=t.consecutive_budget_misses;
    timing["max_consecutive_budget_misses"]=t.max_consecutive_budget_misses;
    timing["hard_limit_exceedances"]=t.hard_limit_exceedances;
    execution_metric(timing.createNestedObject("elapsed"),t.elapsed);
    execution_metric(timing.createNestedObject("scheduled_period"),t.scheduled_period);
    execution_metric(timing.createNestedObject("scheduled_jitter"),t.scheduled_jitter);
    if(t.pending.active) {
        auto pending=timing.createNestedObject("pending");pending["sequence"]=t.pending.sequence;
        pending["origin"]=execution::name(t.pending.origin);pending["phase"]=execution::name(t.pending.phase);
        pending["started_us"]=t.pending.started_us;
    } else timing["pending"]=nullptr;
    if(t.last.available) {
        const auto& last=t.last;auto scan=timing.createNestedObject("last_scan");
        scan["sequence"]=last.sequence;scan["origin"]=execution::name(last.origin);
        scan["started_us"]=last.started_us;scan["completed_us"]=last.completed_us;
        scan["elapsed_us"]=last.elapsed_us;scan["fault_check_elapsed_us"]=last.fault_check_elapsed_us;
        scan["missed_releases"]=last.missed_releases;
        if(last.period_available){scan["period_us"]=last.period_us;scan["jitter_us"]=last.jitter_us;}
        else {scan["period_us"]=nullptr;scan["jitter_us"]=nullptr;}
        const bool coherent=value.scan_available && value.scan_sequence==last.sequence;
        scan["scan_evidence_matches"]=coherent;
        if(coherent) {
            scan["project_generation"]=value.scan_project_generation;
            scan["mode_at_solve"]=mode_name(value.scan_mode_at_solve);scan["mode_after"]=mode_name(value.scan_mode_after);
        }
    } else timing["last_scan"]=nullptr;
    auto watchdog=d.createNestedObject("watchdog");const auto& w=value.watchdog;
    watchdog["task"]="arduino_loop";watchdog["requested_timeout_ms"]=execution::WatchdogTimeoutMs;
    watchdog["requested_action"]="panic_reboot";watchdog["setup_attempted"]=w.setup_attempted;
    watchdog["configuration_confirmed_at_setup"]=w.setup_attempted && w.configuration_result==0;
    watchdog["subscribed_at_setup"]=w.setup_attempted && w.status_result==0;
    watchdog["reconfigure_attempted"]=w.reconfigure_attempted;
    if(w.setup_attempted) {
        watchdog["setup_us"]=w.setup_us;watchdog["init_result"]=w.init_result;
        watchdog["configuration_result"]=w.configuration_result;
        watchdog["subscription_result"]=w.subscription_result;watchdog["status_result"]=w.status_result;
    }
    watchdog["feed_attempts"]=w.feed_attempts;watchdog["feed_successes"]=w.feed_successes;watchdog["feed_errors"]=w.feed_errors;
    watchdog["saturated"]=w.saturated;
    if(w.feed_attempts){watchdog["last_feed_attempt_us"]=w.last_feed_attempt_us;watchdog["last_feed_result"]=w.last_feed_result;}
    else {watchdog["last_feed_attempt_us"]=nullptr;watchdog["last_feed_result"]=nullptr;}
    if(w.feed_successes)watchdog["last_feed_success_us"]=w.last_feed_success_us;
    else watchdog["last_feed_success_us"]=nullptr;
    if(w.first_error!=execution::WatchdogError::None) {
        auto error=watchdog.createNestedObject("first_error");error["phase"]=execution::name(w.first_error);
        error["at_us"]=w.first_error_us;error["result"]=w.first_error_result;
    } else watchdog["first_error"]=nullptr;
    if(value.first_failure.recorded) {
        const auto& f=value.first_failure;auto fault=d.createNestedObject("first_failure");
        fault["kind"]="plc_fault";fault["code"]=f.code;fault["reason"]=f.message;fault["at_us"]=f.at_us;
        fault["project_generation"]=f.project_generation;fault["mode_before"]=mode_name(f.mode_before);
        fault["scan_sequence"]=f.scan_sequence;fault["during_scan"]=f.during_scan;fault["phase"]=execution::name(f.phase);
        fault["cleared"]=f.cleared;if(f.cleared)fault["cleared_us"]=f.cleared_us;else fault["cleared_us"]=nullptr;
        fault["retention"]="until_boot_or_core_reinit";
    } else d["first_failure"]=nullptr;
    if(d.overflowed()||measureJson(d)>4096)return String("{\"error\":\"execution capacity\"}");
    return json(d);
}
