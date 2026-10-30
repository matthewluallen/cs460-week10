// Included only by plc_core.cpp for the explicit generated-runtime profile.
#include "openplc_generated/solver_worker.h"
#include "openplc_generated/program_identity.h"
#include <ArduinoJson.h>
#include <esp_app_desc.h>
#include "acquisition_record.h"
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
#include "../experiments/publication/publication_record.h"
#endif
namespace generated_controller {
using namespace openplc_image;
static OutputGuard guard(20000,18000,5000);
#ifndef OPENPLC_PROCESS_INPUT_MASK
#define OPENPLC_PROCESS_INPUT_MASK 0
#define OPENPLC_BINDING_CONFIG_SHA256 ""
#endif
static process_input::Gate process_gate(OPENPLC_PROCESS_INPUT_MASK,OPENPLC_BINDING_CONFIG_SHA256);
static process_input::Snapshot consumed_inputs,accepted_inputs;
static bool process_ready() {
    process_input::Snapshot snapshot;plc_bindings_process_capture(snapshot);
    return process_gate.ready(snapshot,millis());
}
static uint64_t epoch=1,input_sequence=0,analog_at=0,submitted_at=0;
static int16_t temperature=0;
static bool reset_ready=false,pending=false,stop_job=false,armed=false,started=false;
static uint8_t attempted=255,last_logic=0,last_raw=0,last_effective=0;
static uint64_t completed=0,completed_epoch=0,last_cycle=0,readback_at=0,attempted_at=0;
static uint32_t runtime_sequence=0;
// Retain the force mask actually dispatched with the accepted completed job.
static uint8_t pending_input_force_mask=0,completed_input_force_mask=0;
static uint8_t pending_output_force_mask=0,pending_output_force_values=0;
static uint8_t completed_output_force_mask=0,completed_output_force_values=0;
static bool output_bus_write_ok=false,readback_valid=false;
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
#include "../experiments/publication/publication_firmware.inc"
#endif
// Last attempted RUN job, retained through STOP/failure. Zero means not observed.
// Durations include preemption and bus waits; they are not CPU-only timings.
struct AttemptTiming {
    uint64_t epoch=0,cycle=0,lock_request=0,lock_acquired=0,release=0,deadline=0;
    uint64_t inputs_start=0,inputs_end=0,submitted=0,received=0;
    uint64_t output_start=0,output_end=0;
    uint32_t fault_code=0;
};
static AttemptTiming attempt_timing;
static void fault(uint32_t code,const char* reason) {
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    experimental_publication_record.revoke();
#endif
    if(attempt_timing.cycle && !attempt_timing.fault_code)attempt_timing.fault_code=code;
    plc_fault_raise(code,reason);
}
static bool can_run() {
    return started && reset_ready && process_ready() && worker::state()==worker::State::ready &&
           guard.fault()==OutputGuard::Fault::none && analog_at &&
           uint64_t(esp_timer_get_time())-analog_at<=250000;
}
static void revoke() {
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    experimental_publication_record.revoke();
#endif
    reset_ready=false;process_gate.revoke();
    if(epoch!=UINT64_MAX)++epoch;
}
static void edit_control() {
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    experimental_publication_record.revoke();
#endif
    // Qualification profile requires explicit re-admission after force edits.
    if(s_mode!=PLC_PROGRAM)plc_set_mode(PLC_PROGRAM,SRC_SYSTEM,"force edit");
}
static bool validate_process(const process_input::Snapshot& consumed) {
    process_input::Snapshot current;plc_bindings_process_capture(current);
    if(process_gate.check(current,millis(),&consumed))return true;
    fault(60,"OpenPLC process input expired at publication");return false;
}
static void publish(uint8_t value,bool observe) {
    // Recheck the sample which actually produced this command, not a newer
    // dispatched sample. This is software admission, not wire/contact timing.
    if(value && !validate_process(accepted_inputs))value=0;
    const uint64_t now=esp_timer_get_time();
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    if(experimental_publication_record.valid() && value!=experimental_publication_record.retained().logic)
        experimental_publication_record.revoke();
#endif
    const bool trace_output=attempt_timing.cycle && !attempt_timing.fault_code;
    if(trace_output)attempt_timing.output_start=now;
    // Retry OFF at most once per 20 ms while the owner is scheduled. Base relay
    // bus success and register readback do not prove physical contacts.
    // Shared I2C/lock latency and expansion transport remain unqualified.
    // Routine RUN refresh belongs to accepted-result publication, not idle polls.
    // Collect the pending result before refresh consumes the remaining budget. PROGRAM/fault still retries even if solver hangs;
    // changed commands are never suppressed by this maintenance scheduling.
    const bool maintenance_slot=s_mode==PLC_PROGRAM || (observe && !pending);
    const bool retry_off=value==0 && maintenance_slot &&
                         (attempted==255 || now-attempted_at>=20000);
    if(attempted!=value || retry_off) {
        output_bus_write_ok=io.relay_write_mask(value);
        if(!output_bus_write_ok)fault(58,"generated output write failed");
        // The fixed four-output generated fixture does not control expansion.
        ioexp_set_mech_relay(false);ioexp_set_ssr(0,false);ioexp_set_ssr(1,false);
        attempted=value;attempted_at=esp_timer_get_time();
        observe=true;
    }
    uint8_t readback=0;
    if(observe) {
        readback_valid=io.relay_snapshot(readback);
        if(!readback_valid)fault(59,"generated output readback failed");
    }
    for(unsigned i=0;i<4;++i) {
        s_relay_cmd[i]=(value>>i)&1;
        modbus_server.set_coil(pointmap::RELAY0+i,s_relay_cmd[i]);
        if(observe && readback_valid) {
            s_relay_act[i]=(readback>>i)&1;
            if(s_relay_act[i]!=s_relay_cmd[i])fault(20+i,"generated relay readback");
        }
    }
    if(observe && readback_valid)readback_at=esp_timer_get_time();
    if(trace_output)attempt_timing.output_end=esp_timer_get_time();
}
static void owner_task(void*) {
    for(;;) {
        const uint64_t lock_request=esp_timer_get_time();
        {
            CoreGuard lock;
            const uint64_t lock_acquired=esp_timer_get_time();
            uint64_t now=esp_timer_get_time();
            auto state=worker::state();
            if(state!=worker::State::ready && state!=worker::State::starting)
                fault(50,"OpenPLC worker unavailable");
            if(s_mode==PLC_PROGRAM) {
                guard.stop(now);armed=false;publish(0,false);
            }
            process_input::Snapshot current_inputs;plc_bindings_process_capture(current_inputs);
            // Validate continuously, including before accepting any solver result.
            // Fresh replacements cannot legitimize an expired consumed sample.
            if(s_mode!=PLC_PROGRAM && armed &&
               !process_gate.check(current_inputs,millis(),pending&&!stop_job?&consumed_inputs:nullptr))
                fault(60,"OpenPLC process input changed or stale");
            worker::Result result;
            if(pending && worker::take(result)) {
                pending=false;
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
                if(!stop_job && result.job.epoch==epoch && s_mode==PLC_RUN)publication_start();
#endif
                if(!stop_job && result.job.epoch==attempt_timing.epoch && result.job.cycle==attempt_timing.cycle)
                    attempt_timing.received=esp_timer_get_time();
                if(stop_job) {
                    reset_ready=result.job.epoch==epoch && !result.output.running &&
                                result.status==Status::ok && s_mode==PLC_PROGRAM;
                } else if(s_mode!=PLC_PROGRAM && result.job.epoch==epoch &&
                          validate_process(consumed_inputs) &&
                          guard.complete(result.job,esp_timer_get_time(),result.output.final,
                                         result.status==Status::ok && result.output.runtime_cycle_completed)) {
                    accepted_inputs=consumed_inputs;completed_input_force_mask=pending_input_force_mask;
                    completed_output_force_mask=pending_output_force_mask;
                    completed_output_force_values=pending_output_force_values;
                    last_logic=result.output.logic;last_raw=result.output.raw_inputs;
                    last_effective=result.output.effective_inputs;runtime_sequence=result.output.sequence;
                    last_cycle=result.job.cycle;completed_epoch=result.job.epoch;completed=esp_timer_get_time();
                    ++s_scan_count;++s_heartbeat;
                    s_scan_last=completed-result.job.release_us;
                    if(!s_scan_min || s_scan_last<s_scan_min)s_scan_min=s_scan_last;
                    if(s_scan_last>s_scan_max)s_scan_max=s_scan_last;
                    publish(guard.output(esp_timer_get_time()),true);
                }
            }
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
            publication_finish();
#endif
            now=esp_timer_get_time();
            if(pending && now-submitted_at>=200000)
                fault(51,"OpenPLC result timeout");
            if(s_mode!=PLC_PROGRAM) {
                if(!armed) {
                    armed=process_gate.arm(current_inputs,millis()) && guard.arm(epoch,now);
                    if(!armed)fault(52,"OpenPLC admission fault");
                }
                uint8_t allowed=guard.output(esp_timer_get_time());
                if(guard.fault()!=OutputGuard::Fault::none)fault(53,"OpenPLC deadline fault");
                if(s_mode!=PLC_PROGRAM && !pending) {
                    worker::Request request{};
                    if(guard.release(esp_timer_get_time(),input_sequence+1,request.job)) {
                        attempt_timing={};attempt_timing.epoch=request.job.epoch;attempt_timing.cycle=request.job.cycle;
                        attempt_timing.lock_request=lock_request;attempt_timing.lock_acquired=lock_acquired;
                        attempt_timing.release=request.job.release_us;attempt_timing.deadline=request.job.deadline_us;
                        if(input_sequence==UINT64_MAX || !analog_at || uint64_t(esp_timer_get_time())-analog_at>250000) {
                            fault(54,"OpenPLC input age/sequence");
                        } else {
                            ++input_sequence;
                            attempt_timing.inputs_start=esp_timer_get_time();
                            const bool inputs_valid=io.di_snapshot(request.input.digital);
                            attempt_timing.inputs_end=esp_timer_get_time();
                            request.input.analog[0]=temperature;
                            consumed_inputs=current_inputs;
                            for(unsigned i=0;i<process_input::Count;++i)
                                if(OPENPLC_PROCESS_INPUT_MASK&(1u<<i))
                                    set_process_input(request.input,i+1,consumed_inputs.samples[i].value);
                            request.input.input_force_mask=s_di_force_en;
                            request.input.input_force_values=s_di_force_val;
                            for(unsigned i=0;i<4;++i) {
                                if((s_coil_force_en>>(pointmap::RELAY0+i))&1)request.input.output_force_mask|=1<<i;
                                if((s_coil_force_val>>(pointmap::RELAY0+i))&1)request.input.output_force_values|=1<<i;
                            }
                            request.input.run=true;
                            guard.output(esp_timer_get_time());
                            if(!process_gate.check(current_inputs,millis(),&consumed_inputs))fault(60,"OpenPLC consumed input expired");
                            else if(!inputs_valid)fault(57,"OpenPLC digital acquisition failed");
                            else if(guard.fault()!=OutputGuard::Fault::none || !worker::submit(request))
                                fault(55,"OpenPLC dispatch failure");
                            else {
                                pending_input_force_mask=request.input.input_force_mask;
                                pending_output_force_mask=request.input.output_force_mask;
                                pending_output_force_values=request.input.output_force_values;
                                pending=true;stop_job=false;submitted_at=esp_timer_get_time();attempt_timing.submitted=submitted_at;
                            }
                        }
                    }
                }
                if(s_mode==PLC_PROGRAM) {guard.stop(esp_timer_get_time());armed=false;allowed=0;}
                else allowed=guard.output(esp_timer_get_time());
                publish(allowed,false);
            }
            if(s_mode==PLC_PROGRAM && !pending && !reset_ready && state==worker::State::ready &&
               guard.fault()==OutputGuard::Fault::none) {
                worker::Request stop{};stop.job.epoch=epoch; // stopped runtime cycle, not RUN guard job
                if(worker::submit(stop)){pending=true;stop_job=true;submitted_at=esp_timer_get_time();}
            }
            if(s_mode==PLC_PROGRAM)publish(0,false);
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
            publication_validate();
#endif
            modbus_server.set_register(pointmap::PLC_MODE,s_mode);
            modbus_server.set_register(pointmap::FAULT_COUNT,plc_fault_active_count());
            modbus_server.set_register(pointmap::HEARTBEAT,s_heartbeat);
        }
        vTaskDelay(1);
    }
}
static void begin() {
    started=worker::start(OPENPLC_RETENTION_KEY,20000000,3,1);
    if(!started || xTaskCreatePinnedToCore(owner_task,"openplc_owner",8192,nullptr,7,nullptr,1)!=pdPASS) {
        started=false;fault(56,"OpenPLC task creation");
    }
}
}
String plc_generated_json() {
    CoreGuard lock;using namespace generated_controller;
    StaticJsonDocument<4096> d;
    PlcPanelInputs panel;plc_panel_inputs_capture(panel);
    auto panel_json=d.createNestedObject("panel_inputs");
    panel_json["available"]=panel.available;panel_json["backend"]=panel.generated?"openplc-strucpp":"legacy-vm";
    panel_json["epoch"]=panel.epoch;panel_json["cycle"]=panel.sequence;
    panel_json["completed_us"]=panel.completed_us;panel_json["effective_inputs"]=panel.values;
    panel_json["force_mask"]=panel.force_mask;
    d["schema"]="generated-controller-readback/v1";d["runtime_backend"]="openplc-strucpp";
    d["process_input_mask"]=OPENPLC_PROCESS_INPUT_MASK;
    d["binding_config_sha256"]=OPENPLC_BINDING_CONFIG_SHA256;
    auto inputs=d.createNestedArray("consumed_process_inputs");
    if(completed)for(unsigned i=0;i<process_input::Count;++i)if(OPENPLC_PROCESS_INPUT_MASK&(1u<<i)) {
        const auto& s=accepted_inputs.samples[i];auto v=inputs.createNestedObject();
        v["logical_iw"]=i+1;v["value"]=s.value;v["sampled_ms"]=s.sampled_ms;
        v["max_age_ms"]=s.max_age_ms;v["epoch"]=s.epoch;v["generation"]=s.generation;v["sequence"]=s.sequence;
        v["clock"]="controller_boot_ms";
    }
    d["program_sha256"]=OPENPLC_PROGRAM_SHA256;d["program_hash_basis"]="authored-st-bytes";
    d["program_delivery"]="compiled-firmware";d["runtime_tick_us"]=20000;
    d["mode"]=plc_mode_str();d["reset_ready"]=reset_ready;d["epoch"]=epoch;
    d["worker_state"]=uint32_t(worker::state());if(worker::state()==worker::State::runtime_fault)d["worker_fault_reason"]=worker::runtime_fault_reason();else d["worker_fault_reason"]=nullptr;
    d["guard_fault"]=uint32_t(guard.fault());d["pending"]=pending;
    d["cycle"]=last_cycle;d["completed_epoch"]=completed_epoch;d["runtime_sequence"]=runtime_sequence;
    d["completed_us"]=completed;d["captured_us"]=uint64_t(esp_timer_get_time());
    d["boot_id"]=acquisition_record_boot_id();
    d["clock"]="esp_timer_boot_us";d["raw_inputs"]=last_raw;d["effective_inputs"]=last_effective;
    // Retained accepted-job evidence, not the currently edited force configuration.
    auto forces=d.createNestedObject("accepted_output_forces");
    forces["available"]=completed!=0;forces["epoch"]=completed_epoch;
    forces["cycle"]=last_cycle;forces["completed_us"]=completed;
    forces["mask"]=completed_output_force_mask;forces["values"]=completed_output_force_values;
    d["logic_outputs"]=last_logic;d["attempted_outputs"]=attempted==255?0:attempted;
    d["output_attempt_observed"]=attempted!=255;d["attempted_at_us"]=attempted_at;d["cycle_observed"]=completed!=0;
    uint8_t readback=0;for(unsigned i=0;i<4;++i)if(s_relay_act[i])readback|=1<<i;
    if(readback_at && readback_valid)d["register_readback"]=readback;else d["register_readback"]=nullptr;
    d["readback_at_us"]=readback_at;
    d["readback_basis"]="checked-AW9523-input-register-not-contact-feedback";
    d["output_bus_write_ok"]=output_bus_write_ok;d["readback_valid"]=readback_valid;
    d["output_write_confirmed"]=false;
    const auto* app=esp_app_get_description();char elf[65]{};
    if(app){for(unsigned i=0;i<32;++i)snprintf(elf+2*i,3,"%02x",app->app_elf_sha256[i]);d["elf_sha256"]=elf;}
    auto t=d.createNestedObject("last_attempt_timing");
    t["epoch"]=attempt_timing.epoch;t["cycle"]=attempt_timing.cycle;t["fault_code"]=attempt_timing.fault_code;
    t["lock_request_us"]=attempt_timing.lock_request;t["lock_acquired_us"]=attempt_timing.lock_acquired;
    t["release_us"]=attempt_timing.release;t["deadline_us"]=attempt_timing.deadline;
    t["inputs_start_us"]=attempt_timing.inputs_start;t["inputs_end_us"]=attempt_timing.inputs_end;
    t["submitted_us"]=attempt_timing.submitted;t["received_us"]=attempt_timing.received;
    t["output_start_us"]=attempt_timing.output_start;t["output_end_us"]=attempt_timing.output_end;
    d["physical_contact_observed"]=false;
    if(d.overflowed())return String("{\"error\":\"generated report capacity\"}");
    String text;serializeJson(d,text);return text;
}
