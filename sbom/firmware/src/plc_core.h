// plc_core.h - the PLC coordinator: RUN/PROGRAM mode, I/O forcing, fault table,
// scan-time stats, a source-attributed audit log, and the real scan cycle that
// reads inputs, solves the logic program, and writes outputs at scan boundaries.
#pragma once
#include <Arduino.h>

enum PlcMode   : uint8_t { PLC_PROGRAM = 0, PLC_RUN = 1, PLC_REMOTE = 2 };
enum WriteSource : uint8_t { SRC_SYSTEM = 0, SRC_MODBUS = 1, SRC_USB = 2, SRC_WEB = 3, SRC_LOGIC = 4, SRC_DNP3 = 5, SRC_S7 = 6, SRC_OPCUA = 7, SRC_IEC104 = 8, SRC_ENIP = 9, SRC_BACNET = 10 };

struct FaultEntry { uint16_t code; uint32_t t_ms; bool active; char msg[40]; };

void plc_core_begin();
void plc_core_scan();             // maintenance/inhibition scan (e.g. before project persistence)
void plc_core_scan_scheduled();   // periodic loop scan; same execution/mode/output rules

PlcMode     plc_mode();
const char* plc_mode_str();
void        plc_set_mode(PlcMode m, WriteSource src, const char* who);
#include <atomic>
extern std::atomic<bool> g_bustrace;   // software I2C bus tracer on/off
void        plc_bus_log(uint8_t addr, char rw, const char* name, long val, bool hasval);
// Key-switch gate: program download is allowed only in PROGRAM (locally) or
// REMOTE (the supervised/remote position). RUN protects the running process.
bool        plc_mode_allows_download(WriteSource src);

// Forcing (coils 0-63, DI 0-7). A force overrides logic, but never PROGRAM/fault output inhibition.
void plc_force_coil(uint16_t addr, bool val, WriteSource src, const char* who);
void plc_unforce_coil(uint16_t addr, WriteSource src, const char* who);
bool plc_coil_is_forced(uint16_t addr);
bool plc_coil_forced_val(uint16_t addr);
void plc_force_di(uint8_t idx, bool val, WriteSource src, const char* who);
void plc_unforce_di(uint8_t idx, WriteSource src, const char* who);
bool plc_di_is_forced(uint8_t idx);
bool plc_di_forced_val(uint8_t idx);
void plc_clear_all_forces(WriteSource src, const char* who);
int  plc_force_count();
bool plc_forces_active();

// [CWE-787] Unauthenticated "write tag label". Teaching build copies into a fixed scratch
// buffer with no bound (overflows into the force image -> relay actuation by memory
// corruption); secure build bounds-checks it. Exposed over the network via /api/tag.
void plc_tag_write(const uint8_t* data, size_t len);

// Requested point values (apply force overrides); not physical output feedback.
bool plc_di_live(uint8_t idx);
bool plc_coil_live(uint16_t addr);

// Faults
void plc_fault_raise(uint16_t code, const char* msg);
void plc_fault_clear_all(WriteSource src, const char* who);
int  plc_fault_active_count();
int  plc_fault_list(FaultEntry* out, int maxn);   // returns count copied

// Scan stats
uint32_t plc_scan_last_us();
uint32_t plc_scan_min_us();
uint32_t plc_scan_max_us();
uint32_t plc_scan_count();
bool     plc_first_scan();
uint32_t plc_heartbeat();

// Audit log (rolling ring buffer). i=0 is the most recent entry.
void        plc_log(WriteSource src, const char* srcinfo, const char* fmt, ...);
int         plc_log_count();
bool        plc_log_get(int i, char* buf, int buflen);
const char* plc_src_str(WriteSource s);

// Commanded vs actual for base relays 0-3 (actual = expander readback).
bool plc_relay_commanded(int idx);
bool plc_relay_actual(int idx);

// Last completed coordinator scan, copied under the image lock. These are
// controller reports, not independently observed bus or contact measurements.
#include "plc_engine.h"
struct PlcScanSnapshot {
    bool available=false, saturated=false, solve_attempted=false, solve_ok=false;
    uint64_t sequence=0, started_us=0, completed_us=0;
    // Absolute boot-clock endpoints, not isolated CPU cost; waits/preemption included.
    uint64_t phase_end_us[7]{};
    plc::Instr program[32]{};
    int program_count=0;
    uint32_t project_generation=0;
    uint8_t raw_di=0, input_di=0, logic_outputs=0, applied_outputs=0;
    uint8_t register_readback=0, di_force_mask=0;
    uint64_t coil_force_mask=0;
    int32_t analog[plc::NIW]{};
    PlcMode mode_at_solve=PLC_PROGRAM, mode_after=PLC_PROGRAM;
    uint32_t binding_generation=0, sample_sequence[3]{}, sample_ms[3]{};
};
void plc_scan_capture(PlcScanSnapshot& out);

// Boot-local execution diagnostics, captured together under the image lock.
// Reading/clearing the active fault table never resets this historical evidence.
#include "execution_diagnostics.h"
struct PlcFirstFailure {
    bool recorded=false, cleared=false, during_scan=false;
    uint16_t code=0;
    uint64_t at_us=0, cleared_us=0, scan_sequence=0;
    uint32_t project_generation=0;
    PlcMode mode_before=PLC_PROGRAM;
    execution::Phase phase=execution::Phase::OutsideScan;
    char message[40]{};
};
struct PlcExecutionSnapshot {
    bool supported=true;
    uint64_t captured_us=0;
    uint32_t project_generation=0;
    PlcMode mode=PLC_PROGRAM;
    unsigned active_faults=0;
    execution::Snapshot timing;
    execution::Watchdog watchdog;
    PlcFirstFailure first_failure;
    bool scan_available=false;
    uint64_t scan_sequence=0;
    uint32_t scan_project_generation=0;
    PlcMode scan_mode_at_solve=PLC_PROGRAM, scan_mode_after=PLC_PROGRAM;
};
void plc_execution_capture(PlcExecutionSnapshot& out);
// All SDK operations are done by the loop outside the image lock. These calls
// retain their results only; zero denotes ESP_OK, including in host tests.
void plc_execution_watchdog_setup(int init_result, bool reconfigured, int configuration_result,
                                  int subscription_result, int status_result);
void plc_execution_watchdog_feed(int result);

// Compact completed-scan DI view for the panel; no direct bus sampling.
struct PlcPanelInputs {
    bool available=false,generated=false;
    uint64_t sequence=0,completed_us=0,epoch=0;
    uint8_t values=0,force_mask=0;
};
void plc_panel_inputs_capture(PlcPanelInputs& out);

#ifdef OPENPLC_GENERATED_CONTROLLER
String plc_generated_json();
#endif
