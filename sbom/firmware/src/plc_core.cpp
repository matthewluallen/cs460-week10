#include "diagnostic_log.h"
#include "plc_core.h"
#include "plc_auth.h"
#include "plc_tags.h"
#include "m5stamplc_io.h"
#include "modbus_tcp_server.h"
#include "io_expansion.h"
#include "plc_engine.h"
#include "process_bindings.h"
#include "loop_timing.h"
#include <stdarg.h>
#include <cstring>
#include <esp_timer.h>

// The logic engine and program live in main.cpp; shared here.
extern plc::Engine g_engine;
extern plc::Instr  g_prog[32];
extern int         g_prog_n;

// ---- state ----------------------------------------------------------------
static PlcMode  s_mode = PLC_PROGRAM;
static PlcScanSnapshot s_last_scan;
struct CoreGuard {
    CoreGuard() { plc_image_lock(); }
    ~CoreGuard() { plc_image_unlock(); }
};
#ifdef ENABLE_CWE_DEMOS
// [CWE-787 / CWE-120] Teaching "build break": a fixed tag-label scratch buffer placed in
// the SAME object as the I/O force image, immediately before the force masks. The
// unauthenticated writer plc_tag_write() copies attacker-controlled bytes into scratch[]
// with NO bounds check, so an overflow runs straight on into coil_force_val/en and forces
// a relay by MEMORY CORRUPTION. It never calls the audited plc_force_coil(), so the force
// appears with no source/who in the audit log. The struct guarantees scratch sits right
// before the masks (offsetof is the exploit's whole "memory map"); tools/derive-force-offset.py
// re-derives it from each build. The secure build (#else) keeps the masks with NO adjacent
// attacker-writable buffer and bounds-checks plc_tag_write(), so this overflow cannot occur.
struct ForceImage {
    char     scratch[32];      // overflow source (member offset 0)
    uint64_t coil_force_val;   // offset 32: an 8-byte overflow past scratch reaches this
    uint64_t coil_force_en;    // offset 40: a 16-byte overflow reaches this
    uint8_t  di_force_val, di_force_en;
};
static ForceImage s_fi = {};
#define s_coil_force_en  s_fi.coil_force_en
#define s_coil_force_val s_fi.coil_force_val
#define s_di_force_en    s_fi.di_force_en
#define s_di_force_val   s_fi.di_force_val
#else
static uint64_t s_coil_force_en = 0, s_coil_force_val = 0;   // coils 0-63
static uint8_t  s_di_force_en = 0,  s_di_force_val = 0;      // DI 0-7
#endif
static bool     s_first_scan = true;
static uint32_t s_scan_last = 0, s_scan_min = 0, s_scan_max = 0, s_scan_count = 0, s_heartbeat = 0;
static bool     s_relay_cmd[4] = {false}, s_relay_act[4] = {false}, s_relay_mismatch[4] = {false};

static const int   FAULT_MAX = 16;
static FaultEntry  s_faults[FAULT_MAX];
static int         s_fault_n = 0;

static const int   LOG_MAX = 32, LOG_LEN = 108;
static char        s_log[LOG_MAX][LOG_LEN];
static int         s_log_head = 0;   // index of next write
static int         s_log_count = 0;

#ifdef OPENPLC_GENERATED_CONTROLLER
#include "plc_generated_controller.inc"
#endif

const char* plc_src_str(WriteSource s) {
    switch (s) { case SRC_MODBUS: return "MODBUS"; case SRC_DNP3: return "DNP3"; case SRC_S7: return "S7"; case SRC_OPCUA: return "OPCUA"; case SRC_IEC104: return "IEC104"; case SRC_ENIP: return "ENIP"; case SRC_BACNET: return "BACNET"; case SRC_USB: return "USB";
                 case SRC_WEB: return "WEB"; case SRC_LOGIC: return "LOGIC"; default: return "SYSTEM"; }
}

void plc_log(WriteSource src, const char* srcinfo, const char* fmt, ...) {
    char body[72];
    va_list ap; va_start(ap, fmt); vsnprintf(body, sizeof(body), fmt, ap); va_end(ap);
    char* dst = s_log[s_log_head];
    snprintf(dst, LOG_LEN, "%lu %s%s%s %s", (unsigned long)millis(), plc_src_str(src),
             srcinfo && srcinfo[0] ? "/" : "", srcinfo ? srcinfo : "", body);
    s_log_head = (s_log_head + 1) % LOG_MAX;
    if (s_log_count < LOG_MAX) s_log_count++;
}
int plc_log_count() { return s_log_count; }
bool plc_log_get(int i, char* buf, int buflen) {
    if (i < 0 || i >= s_log_count) return false;
    int idx = (s_log_head - 1 - i + LOG_MAX * 2) % LOG_MAX;   // 0 = most recent
    strncpy(buf, s_log[idx], buflen - 1); buf[buflen - 1] = 0;
    return true;
}

// ---- mode -----------------------------------------------------------------
std::atomic<bool> g_bustrace{false};
void plc_bus_log(uint8_t addr, char rw, const char* name, long val, bool hasval) {
    if (!g_bustrace) return;
    if (hasval) diagnostic_log_printf(diagnostics::Origin::Bus,"[BUS] %lu %c 0x%02X %s=%ld\n", (unsigned long)millis(), rw, addr, name, val);
    else        diagnostic_log_printf(diagnostics::Origin::Bus,"[BUS] %lu %c 0x%02X %s\n", (unsigned long)millis(), rw, addr, name);
}
PlcMode plc_mode() { CoreGuard guard; return s_mode; }
const char* plc_mode_str() { const auto m = plc_mode(); return m == PLC_RUN ? "RUN" : m == PLC_REMOTE ? "REMOTE" : "PROGRAM"; }
void plc_set_mode(PlcMode m, WriteSource src, const char* who) {
    CoreGuard guard;
    if (m != PLC_PROGRAM && m != PLC_RUN && m != PLC_REMOTE) {
        plc_log(src, who, "mode rejected: invalid value"); return;
    }
#ifdef OPENPLC_GENERATED_CONTROLLER
    if(m==PLC_REMOTE || (m!=PLC_PROGRAM && (!generated_controller::can_run() || plc_fault_active_count()))) {
        plc_log(src,who,"generated mode refused: reset/input/fault or unsupported REMOTE");return;
    }
    if(m!=s_mode)generated_controller::revoke();
#else
    if (m != PLC_PROGRAM && (plc_fault_active_count() || plc::validate_program(g_prog, g_prog_n))) {
        plc_log(src, who, "mode rejected: fault or invalid program"); return;
    }
    if (m != PLC_PROGRAM && !plc_bindings_ready(g_prog,g_prog_n,m != s_mode)) {
        plc_log(src,who,"mode rejected: process inputs not ready"); return;
    }
    #endif
    if (m == PLC_PROGRAM) { g_engine.reset(); s_first_scan = true; }
    if (m == s_mode) return;
    s_mode = m;
    plc_log(src, who, "mode -> %s", plc_mode_str());
}

bool plc_mode_allows_download(WriteSource src) {
    CoreGuard guard;
#ifdef OPENPLC_GENERATED_CONTROLLER
    return false; // compiled program; never acknowledge custom-VM IR as execution
#endif
    // REMOTE: any source may download (the position that trusts the network).
    if (s_mode == PLC_REMOTE) return true;
    // PROGRAM: local USB download only (outputs are already in safe state).
    if (s_mode == PLC_PROGRAM) return src == SRC_USB;
    // RUN: locked - no reprogramming while the process is executing.
    return false;
}

// ---- forcing --------------------------------------------------------------
void plc_force_coil(uint16_t a, bool v, WriteSource src, const char* who) { CoreGuard guard;
#ifdef OPENPLC_GENERATED_CONTROLLER
    generated_controller::edit_control();
#endif

    if (a >= 64) return;
    s_coil_force_en |= (1ULL << a);
    if (v) s_coil_force_val |= (1ULL << a); else s_coil_force_val &= ~(1ULL << a);
    plc_log(src, who, "FORCE coil %u = %d", a, v ? 1 : 0);
}
void plc_unforce_coil(uint16_t a, WriteSource src, const char* who) { CoreGuard guard;
#ifdef OPENPLC_GENERATED_CONTROLLER
    generated_controller::edit_control();
#endif

    if (a >= 64) return;
    s_coil_force_en &= ~(1ULL << a);
    plc_log(src, who, "UNFORCE coil %u", a);
}
bool plc_coil_is_forced(uint16_t a) { CoreGuard guard; return a < 64 && (s_coil_force_en >> a) & 1ULL; }
bool plc_coil_forced_val(uint16_t a) { CoreGuard guard; return a < 64 && (s_coil_force_val >> a) & 1ULL; }
void plc_force_di(uint8_t i, bool v, WriteSource src, const char* who) { CoreGuard guard;
#ifdef OPENPLC_GENERATED_CONTROLLER
    generated_controller::edit_control();
#endif

    if (i >= 8) return;
    s_di_force_en |= (1 << i);
    if (v) s_di_force_val |= (1 << i); else s_di_force_val &= ~(1 << i);
    plc_log(src, who, "FORCE di%u = %d", i, v ? 1 : 0);
}
void plc_unforce_di(uint8_t i, WriteSource src, const char* who) { CoreGuard guard;
#ifdef OPENPLC_GENERATED_CONTROLLER
    generated_controller::edit_control();
#endif

    if (i >= 8) return;
    s_di_force_en &= ~(1 << i);
    plc_log(src, who, "UNFORCE di%u", i);
}
bool plc_di_is_forced(uint8_t i) { CoreGuard guard; return i < 8 && (s_di_force_en >> i) & 1; }
bool plc_di_forced_val(uint8_t i) { CoreGuard guard; return i < 8 && (s_di_force_val >> i) & 1; }
void plc_clear_all_forces(WriteSource src, const char* who) { CoreGuard guard;
#ifdef OPENPLC_GENERATED_CONTROLLER
    generated_controller::edit_control();
#endif

    s_coil_force_en = 0; s_di_force_en = 0;
    plc_log(src, who, "CLEAR ALL FORCES");
}
int plc_force_count() { CoreGuard guard;
    int n = 0;
    for (int i = 0; i < 64; i++) if ((s_coil_force_en >> i) & 1ULL) n++;
    for (int i = 0; i < 8; i++)  if ((s_di_force_en >> i) & 1)   n++;
    return n;
}
bool plc_forces_active() { CoreGuard guard; return s_coil_force_en != 0 || s_di_force_en != 0; }

// [CWE-787] Unauthenticated "write tag label". Teaching build: an unbounded copy into the
// scratch buffer that sits just before the force image, so an overlong write corrupts the
// force masks and actuates a relay with no audit trail. Secure build: a bounded copy into a
// standalone buffer - the force image is never reachable this way.
void plc_tag_write(const uint8_t* data, size_t len) {
#ifdef ENABLE_CWE_DEMOS
    memcpy(s_fi.scratch, data, len);                     // CWE-787: no bounds check -> overflows s_fi.*
#else
    static char s_tag[32];
    size_t n = len < sizeof(s_tag) - 1 ? len : sizeof(s_tag) - 1;   // hardened: clamp to the buffer
    memcpy(s_tag, data, n); s_tag[n] = 0;
#endif
}

bool plc_di_live(uint8_t i)   { return plc_di_is_forced(i) ? plc_di_forced_val(i) : io.di_read(i); }
bool plc_coil_live(uint16_t a){ return plc_coil_is_forced(a) ? plc_coil_forced_val(a) : modbus_server.get_coil(a); }

// ---- faults ---------------------------------------------------------------
void plc_fault_raise(uint16_t code, const char* msg) {
    CoreGuard guard;
#ifdef OPENPLC_PUBLICATION_EXPERIMENT
    generated_controller::experimental_publication_record.revoke();
#endif
    // Even an unrecordable fault (full table) inhibits execution. Clearing the
    // table never rearms outputs; a separate mode command is required.
    plc_set_mode(PLC_PROGRAM, SRC_SYSTEM, "fault");
    for (int i = 0; i < s_fault_n; i++)
        if (s_faults[i].code == code && s_faults[i].active) return;   // already active
    if (s_fault_n < FAULT_MAX) {
        FaultEntry& f = s_faults[s_fault_n++];
        f.code = code; f.t_ms = millis(); f.active = true;
        strncpy(f.msg, msg, sizeof(f.msg) - 1); f.msg[sizeof(f.msg) - 1] = 0;
        plc_log(SRC_SYSTEM, "", "FAULT %u: %s", code, msg);
    }
}
void plc_fault_clear_all(WriteSource src, const char* who) {
    CoreGuard guard;
    s_fault_n = 0;
    for (int i = 0; i < 4; i++) s_relay_mismatch[i] = false;
    plc_log(src, who, "faults cleared");
}
int plc_fault_active_count() { CoreGuard guard; int n = 0; for (int i = 0; i < s_fault_n; i++) if (s_faults[i].active) n++; return n; }
int plc_fault_list(FaultEntry* out, int maxn) { CoreGuard guard;
    int n = 0; for (int i = 0; i < s_fault_n && n < maxn; i++) out[n++] = s_faults[i]; return n;
}

// ---- scan stats -----------------------------------------------------------
uint32_t plc_scan_last_us() { return s_scan_last; }
uint32_t plc_scan_min_us()  { return s_scan_min; }
uint32_t plc_scan_max_us()  { return s_scan_max; }
uint32_t plc_scan_count()   { return s_scan_count; }
bool     plc_first_scan()   { return s_first_scan; }
uint32_t plc_heartbeat()    { return s_heartbeat; }
bool plc_relay_commanded(int i) { return (i >= 0 && i < 4) ? s_relay_cmd[i] : false; }
bool plc_relay_actual(int i)    { return (i >= 0 && i < 4) ? s_relay_act[i] : false; }

void plc_scan_capture(PlcScanSnapshot& out) { CoreGuard guard; out=s_last_scan; }
void plc_panel_inputs_capture(PlcPanelInputs& out) {
    CoreGuard guard;
    out=PlcPanelInputs{};
#ifdef OPENPLC_GENERATED_CONTROLLER
    // Read only accepted completed work, never current inputs or a pending job.
    // Retain the last observation through STOP; the panel displays its age.
    out.generated=true;
    out.available=generated_controller::completed!=0;
    out.sequence=generated_controller::last_cycle;
    out.epoch=generated_controller::completed_epoch;
    out.completed_us=generated_controller::completed;
    out.values=generated_controller::last_effective;
    out.force_mask=generated_controller::completed_input_force_mask;
#else
    out.available=s_last_scan.available;out.sequence=s_last_scan.sequence;
    out.completed_us=s_last_scan.completed_us;out.values=s_last_scan.input_di;
    out.force_mask=s_last_scan.di_force_mask;
#endif
}

// ---- init + scan ----------------------------------------------------------
void plc_core_begin() {
    CoreGuard guard;
    s_mode = PLC_PROGRAM;
    s_last_scan = PlcScanSnapshot{};
    plc_bindings_begin();
    s_coil_force_en = s_coil_force_val = 0;
    s_di_force_en = s_di_force_val = 0;
    s_fault_n = 0;
    g_engine.reset();
    for (const auto& p : pointmap::PARAMETERS) modbus_server.set_register(p.address, p.value);
    s_first_scan = true;
    s_scan_min = 0; s_scan_max = 0; s_scan_count = 0;
    plc_log(SRC_SYSTEM, "", "PLC core start, mode=PROGRAM (outputs inhibited)");
#ifdef OPENPLC_GENERATED_CONTROLLER
    generated_controller::begin();
#endif
}

#ifdef OPENPLC_PUBLICATION_EXPERIMENT
static unsigned publication_input_depth=0;
static bool publication_input_writing=false;
void plc_publication_inputs_begin() {
    CoreGuard lock;
    if(publication_input_depth++==0) {
        auto& record=generated_controller::experimental_publication_record;
        publication_input_writing=record.valid() && record.begin();
    }
}
void plc_publication_inputs_end() {
    CoreGuard lock;
    if(!publication_input_depth)return;
    if(--publication_input_depth==0 && publication_input_writing) {
        auto& record=generated_controller::experimental_publication_record;
        const auto accepted=record.retained();
        record.finish(accepted,generated_controller::publication_current(),millis());
        publication_input_writing=false;
    }
}
#endif

void plc_core_scan() {
#ifdef OPENPLC_GENERATED_CONTROLLER
    // Only acquisition remains in the Arduino loop; no generated solve or output.
    plc_bindings_refresh();
    const float sample=io.temp_read();
    CoreGuard lock;
    if(isfinite(sample) && sample>=-3276.8f && sample<=3276.7f) {
        generated_controller::temperature=int16_t(sample*10);
        generated_controller::analog_at=esp_timer_get_time();
        modbus_server.set_register(pointmap::TEMP_C_X10,uint16_t(generated_controller::temperature));
    }
    return;
#endif
    PlcScanTiming whole_scan; // destructor runs after the image guard releases
    uint32_t t0 = micros();
    const uint64_t scan_started_us=esp_timer_get_time();
    uint64_t phase_end_us[7]{};

    // 1. INPUT IMAGE: latch digital inputs (with force override) and analog.
    bool digital[8];
    for (int i = 0; i < 8; i++) digital[i] = io.di_read(i);
    // Hardware reads stay outside the VM lock. Apply the current force image
    // and publish every input together with the solve below.

    // Driver annotation only: the AW9523 owns the opto inputs; 0x43 is
    // the separate button/display expander. No per-transaction ACK is retained.
    plc_bus_log(0x59, 'R', "poll DI (opto x8)", 0, false);
    // Analog sensors are I2C-heavy; read them a few times a second, not every scan,
    // so the scan stays fast and deterministic (digital I/O runs every scan).
    static uint32_t s_analog_div = 0;
    static int s_tempx10 = 0;
    if ((s_analog_div++ % 5) == 0) {
        // Read the (slow, I2C) sensors into locals OUTSIDE the image lock...
        s_tempx10 = (int)(io.temp_read() * 10.0f);
        uint16_t r_vin = (uint16_t)(io.voltage_read() * 1000.0f);
        uint16_t r_cur = (uint16_t)(io.current_read() * 1000.0f);
        int16_t v1 = 0, v2 = 0; int32_t i1 = 0, i2 = 0;
        bool ioe = ioexp_read(&v1, &i1, &v2, &i2);
        if (g_bustrace) {
            plc_bus_log(0x48, 'R', "LM75 temp x10", s_tempx10, true);
            plc_bus_log(0x40, 'R', "INA226 Vin mV", r_vin, true);
            plc_bus_log(0x40, 'R', "INA226 I mA", r_cur, true);
            if (ioe) plc_bus_log(ioexp_addr(), 'R', "IO-exp CH1 mV", v1, true);
        }
        // ...then publish the analog group atomically (board review #13, cond 1),
        // so a Modbus read of regs 0-2 / 10-13 never sees a torn set.
        plc_image_lock();
        modbus_server.set_register(pointmap::VIN_MV, r_vin);
        modbus_server.set_register(pointmap::CURRENT_MA, r_cur);
        modbus_server.set_register(pointmap::TEMP_C_X10, (uint16_t)s_tempx10);
        if (ioe) {
            modbus_server.set_register(pointmap::AIO_CH1_MV, (uint16_t)v1);
            modbus_server.set_register(pointmap::AIO_CH1_UA, (uint16_t)(i1 < 0 ? 0 : (i1 > 65535 ? 65535 : i1)));
            modbus_server.set_register(pointmap::AIO_CH2_MV, (uint16_t)v2);
            modbus_server.set_register(pointmap::AIO_CH2_UA, (uint16_t)(i2 < 0 ? 0 : (i2 > 65535 ? 65535 : i2)));
        }
        plc_image_unlock();
    }
    phase_end_us[0]=esp_timer_get_time();
    plc_bindings_refresh();
    phase_end_us[1]=esp_timer_get_time();
    // Mode changes can reset VM memory; serialize its input publication too.
    CoreGuard output_guard;
    for (int i = 0; i < 8; ++i) {
        bool value = plc_di_is_forced(i) ? plc_di_forced_val(i) : digital[i];
        modbus_server.set_coil(pointmap::DI_MIRROR_BASE + i, value);
        g_engine.set_input(i, value);
    }
    g_engine.set_analog(pointmap::IW_BASE_TEMPERATURE, s_tempx10);          // program temp interlock reads IW0
    g_engine.set_analog(pointmap::IW_TEMPERATURE_SETPOINT, int16_t(modbus_server.get_register(pointmap::SETPOINT_TEMPX10)));
    g_engine.set_analog(pointmap::IW_HIGH_TEMPERATURE_LIMIT, int16_t(modbus_server.get_register(pointmap::ALARM_TEMPX10)));
    g_engine.set_analog(pointmap::IW_CYCLE_MS, modbus_server.get_register(pointmap::CYCLE_MS));
    g_engine.set_mem(0, plc_coil_live(pointmap::SOFT_ESTOP));      // MX0 = soft_estop (coil 8)
    g_engine.set_mem(1, s_first_scan);          // MX1 = first-scan bit
    g_engine.set_mem(3, plc_coil_live(pointmap::CMD_RUN));      // MX3 = cmd_run (coil 9)

    plc_bindings_scan(g_engine,g_prog,g_prog_n,s_mode != PLC_PROGRAM);

    // Fixed-size local evidence; publish only after all final output handling.
    PlcScanSnapshot evidence;
    evidence.available=true;
    evidence.sequence=s_last_scan.sequence==UINT64_MAX ? UINT64_MAX : s_last_scan.sequence+1;
    evidence.saturated=s_last_scan.sequence==UINT64_MAX;
    evidence.started_us=scan_started_us;
    evidence.program_count=g_prog_n;
    memcpy(evidence.program,g_prog,sizeof(evidence.program));
    evidence.mode_at_solve=s_mode;
    evidence.di_force_mask=s_di_force_en;evidence.coil_force_mask=s_coil_force_en;
    for(int i=0;i<8;++i){if(digital[i])evidence.raw_di|=1<<i;if(g_engine.input(i))evidence.input_di|=1<<i;}
    for(int i=0;i<plc::NIW;++i)evidence.analog[i]=g_engine.analog(i);
    { PlcBindingsSnapshot binding;plc_bindings_capture(binding);
      evidence.binding_generation=binding.generation;
      for(int i=0;i<3;++i){evidence.sample_sequence[i]=binding.samples[i].sequence;evidence.sample_ms[i]=binding.samples[i].sampled;} }
    phase_end_us[2]=esp_timer_get_time();
    // 2. SOLVE. Faults enter PROGRAM; bad IR executes no instructions.
    evidence.solve_attempted=s_mode!=PLC_PROGRAM;
    if(evidence.solve_attempted) {
        evidence.solve_ok=g_engine.scan(g_prog,g_prog_n,millis());
        if(!evidence.solve_ok)plc_fault_raise(30,"invalid runtime program");
    }
    for(int i=0;i<4;++i)if(g_engine.relay(i))evidence.logic_outputs|=1<<i;
    phase_end_us[3]=esp_timer_get_time();
    const bool outputs_enabled = s_mode != PLC_PROGRAM && !plc_fault_active_count();

    // 3. OUTPUT IMAGE -> physical. Base relays 0-3 map to coils 1-4.
    for (int k = 0; k < 4; k++) {
        uint16_t coil = pointmap::RELAY0 + k;
        bool commanded = outputs_enabled ? g_engine.relay(k) : false;
        modbus_server.set_coil(coil, commanded);
        bool phys = outputs_enabled && (plc_coil_is_forced(coil) ? plc_coil_forced_val(coil) : commanded);
        io.relay_set(k, phys);
        s_relay_cmd[k] = phys;
    }
    // Expansion requests remain inspectable, but share the same inhibition.
    bool estop = outputs_enabled && plc_coil_live(pointmap::ESTOP);
    if (ioexp_present()) {
        ioexp_set_mech_relay(estop);
        ioexp_set_ssr(0, outputs_enabled && plc_coil_live(pointmap::SSR1));
        ioexp_set_ssr(1, outputs_enabled && plc_coil_live(pointmap::SSR2));
        uint8_t actual = 0;
        const uint8_t expected = (estop ? 0x10 : 0) |
            (outputs_enabled && plc_coil_live(pointmap::SSR1) ? 1 : 0) |
            (outputs_enabled && plc_coil_live(pointmap::SSR2) ? 2 : 0);
        if (!ioexp_output_readback(&actual)) plc_fault_raise(40, "expansion output read failed");
        else if (actual != expected) plc_fault_raise(41, "expansion cmd/register mismatch");
    }

    phase_end_us[4]=esp_timer_get_time();
    // commanded vs actual (expander readback) for base relays
    for (int k = 0; k < 4; k++) {
        s_relay_act[k] = io.relay_get(k);
        if (s_relay_act[k] != s_relay_cmd[k]) {
            if (!s_relay_mismatch[k]) { s_relay_mismatch[k] = true; plc_fault_raise(20 + k, "relay cmd/actual mismatch"); }
        } else s_relay_mismatch[k] = false;
    }

    phase_end_us[5]=esp_timer_get_time();
    uint32_t dt = micros() - t0;
    if (dt > 100000) plc_fault_raise(10, "scan overrun >100ms");
    // A fault discovered during this scan must inhibit outputs in this scan.
    if (outputs_enabled && s_mode == PLC_PROGRAM) {
        for (int k = 0; k < 4; k++) {
            io.relay_set(k, false); s_relay_cmd[k] = false;
            modbus_server.set_coil(pointmap::RELAY0 + k, false);
            s_relay_act[k] = io.relay_get(k);
        }
        ioexp_set_mech_relay(false); ioexp_set_ssr(0, false); ioexp_set_ssr(1, false);
    }

    // 4. STATUS registers + scan stats
    s_scan_last = dt;
    if (s_scan_min == 0 || dt < s_scan_min) s_scan_min = dt;
    if (dt > s_scan_max) s_scan_max = dt;
    s_scan_count++; s_heartbeat++;
    { uint8_t mb[6]; plc_device_mac_bytes(mb);
      plc_image_lock();   // publish the status group + MAC atomically (BR#13 cond 1)
      modbus_server.set_register(pointmap::PLC_MODE, (uint16_t)s_mode);
      modbus_server.set_register(pointmap::SCAN_LAST_US, (uint16_t)(s_scan_last > 65535 ? 65535 : s_scan_last));
      modbus_server.set_register(pointmap::SCAN_MAX_US, (uint16_t)(s_scan_max > 65535 ? 65535 : s_scan_max));
      modbus_server.set_register(pointmap::FAULT_COUNT, (uint16_t)plc_fault_active_count());
      modbus_server.set_register(pointmap::FORCES_ACTIVE, plc_forces_active() ? 1 : 0);
      modbus_server.set_register(pointmap::HEARTBEAT, (uint16_t)s_heartbeat);
      // [CWE-200] device-identity registers: MAC leaked unauthenticated over Modbus
      // FC3 (map-v2 regs 40-42), the value the 'service' account password derives from.
      modbus_server.set_register(pointmap::DEVICE_MAC_WORD0, (uint16_t)((mb[0] << 8) | mb[1]));
      modbus_server.set_register(pointmap::DEVICE_MAC_WORD1, (uint16_t)((mb[2] << 8) | mb[3]));
      modbus_server.set_register(pointmap::DEVICE_MAC_WORD2, (uint16_t)((mb[4] << 8) | mb[5]));
      modbus_server.set_register(pointmap::POINT_MAP_VERSION, pointmap::VERSION);
      for (unsigned i = 0; i < 16; ++i)
          modbus_server.set_register(pointmap::DIGEST_BASE + i, pointmap::DIGEST_WORDS[i]);
      plc_image_unlock(); }

    if (outputs_enabled && s_mode != PLC_PROGRAM) s_first_scan = false;
    evidence.mode_after=s_mode;
    for(int i=0;i<4;++i){if(s_relay_cmd[i])evidence.applied_outputs|=1<<i;if(s_relay_act[i])evidence.register_readback|=1<<i;}
    evidence.completed_us=esp_timer_get_time();
    phase_end_us[6]=evidence.completed_us;
    memcpy(evidence.phase_end_us,phase_end_us,sizeof(phase_end_us));
    s_last_scan=evidence; // still inside output_guard; never a partly published scan

}
