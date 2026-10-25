#include "plc_diag.h"
#include "plc_core.h"
#include "plc_point_map.h"
#include "m5stamplc_io.h"
#include <M5Unified.h>
#include "io_expansion.h"
#include "plc_auth.h"
#include <Wire.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_chip_info.h>
#include <esp_app_format.h>
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdlib>

extern M5StampLCIO io;

// ---------------------------------------------------------------------------
// RTC "black box": lives in RTC slow memory, so it survives a panic reboot
// (RTC_NOINIT_ATTR is NOT cleared on soft reset - only on power loss). We keep
// a rolling snapshot of the controller's last known state; after a crash the
// next boot can print exactly what it was doing. This is our field post-mortem.
// ---------------------------------------------------------------------------
#define BB_MAGIC 0x53504C43u   // 'SPLC'
struct BlackBox {
    uint32_t magic;
    uint32_t boot_count;
    uint32_t crash_count;
    int      last_reset_reason;   // esp_reset_reason() at THIS boot
    // last checkpoint of the PREVIOUS run (what it was doing before it died):
    uint32_t last_uptime_s;
    int      last_mode;
    uint32_t last_scan_us;
    uint32_t last_scan_max_us;
    int      last_faults;
    int      last_forces;
    uint32_t min_heap;            // lowest free heap ever seen
    char     last_cmd[24];        // last console command entered
    char     phase[16];           // coarse phase breadcrumb
};
static RTC_NOINIT_ATTR BlackBox s_bb;

static int s_reset_reason = 0;
static bool s_was_crash = false;

static bool reason_is_crash(int r) {
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
           r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

const char* plc_diag_reset_reason_str() {
    switch (s_reset_reason) {
        case ESP_RST_POWERON:  return "power-on";
        case ESP_RST_EXT:      return "external reset pin";
        case ESP_RST_SW:       return "software reset (esp_restart)";
        case ESP_RST_PANIC:    return "PANIC / unhandled exception";
        case ESP_RST_INT_WDT:  return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT:      return "other watchdog";
        case ESP_RST_DEEPSLEEP:return "wake from deep sleep";
        case ESP_RST_BROWNOUT: return "brownout (low/unstable power)";
        case ESP_RST_SDIO:     return "SDIO reset";
        default:               return "unknown";
    }
}
bool plc_diag_last_was_crash() { return s_was_crash; }

void plc_diag_begin() {
    s_reset_reason = (int)esp_reset_reason();
    if (s_bb.magic != BB_MAGIC) {          // cold boot / first flash: init
        memset(&s_bb, 0, sizeof(s_bb));
        s_bb.magic = BB_MAGIC;
        s_bb.min_heap = esp_get_free_heap_size();
        strncpy(s_bb.phase, "boot", sizeof(s_bb.phase) - 1);
    }
    s_bb.boot_count++;
    s_bb.last_reset_reason = s_reset_reason;
    s_was_crash = reason_is_crash(s_reset_reason);
    if (s_was_crash) {
        s_bb.crash_count++;
        Serial.println(F("\n================ [BLACKBOX] PREVIOUS BOOT CRASHED ================"));
        Serial.printf ("  reset reason : %s\n", plc_diag_reset_reason_str());
        Serial.printf ("  last state   : uptime=%lus mode=%d scan=%luus(max %lu) faults=%d forces=%d\n",
                       (unsigned long)s_bb.last_uptime_s, s_bb.last_mode,
                       (unsigned long)s_bb.last_scan_us, (unsigned long)s_bb.last_scan_max_us,
                       s_bb.last_faults, s_bb.last_forces);
        Serial.printf ("  last command : '%s'    phase: %s    min-heap: %lu\n",
                       s_bb.last_cmd, s_bb.phase, (unsigned long)s_bb.min_heap);
        Serial.printf ("  crash count  : %lu of %lu boots\n",
                       (unsigned long)s_bb.crash_count, (unsigned long)s_bb.boot_count);
        Serial.println(F("  -> run 'support' and send us the block for remote diagnosis."));
        Serial.println(F("=================================================================\n"));
    }
}

void plc_diag_note_cmd(const char* c) {
    if (!c) return;
    strncpy(s_bb.last_cmd, c, sizeof(s_bb.last_cmd) - 1);
    s_bb.last_cmd[sizeof(s_bb.last_cmd) - 1] = 0;
}
void plc_diag_note_phase(const char* p) {
    if (!p) return;
    strncpy(s_bb.phase, p, sizeof(s_bb.phase) - 1);
    s_bb.phase[sizeof(s_bb.phase) - 1] = 0;
}

void plc_diag_loop(uint32_t now) {
    static uint32_t last = 0;
    if (now - last < 1000) return;         // 1 Hz checkpoint - cheap
    last = now;
    s_bb.last_uptime_s    = now / 1000;
    s_bb.last_mode        = plc_mode();
    s_bb.last_scan_us     = plc_scan_last_us();
    s_bb.last_scan_max_us = plc_scan_max_us();
    s_bb.last_faults      = plc_fault_active_count();
    s_bb.last_forces      = plc_force_count();
    uint32_t h = esp_get_free_heap_size();
    if (h < s_bb.min_heap) s_bb.min_heap = h;
}

// ---------------------------------------------------------------------------
// console command handlers
// ---------------------------------------------------------------------------
static void diag_mem() {
    Serial.printf("heap free      : %lu B   (min-ever %lu B, largest block %lu B)\n",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    size_t ps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (ps) Serial.printf("psram          : %lu B free of %lu B\n",
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned long)ps);
    else    Serial.println("psram          : none");
    Serial.printf("loop stack free: %lu B (high-water mark)\n",
        (unsigned long)uxTaskGetStackHighWaterMark(NULL));
}

static void diag_tasks() {
#if (configUSE_TRACE_FACILITY == 1)
    const UBaseType_t N = 24;
    TaskStatus_t st[N];
    uint32_t total = 0;
    UBaseType_t n = uxTaskGetSystemState(st, N, &total);
    Serial.printf("%-16s %-4s %-4s %-10s %-4s\n", "task", "core", "prio", "state", "stackfree(B)");
    for (UBaseType_t i = 0; i < n; i++) {
        const char* stt = "?";
        switch (st[i].eCurrentState) {
            case eRunning:   stt = "run";   break;
            case eReady:     stt = "ready"; break;
            case eBlocked:   stt = "block"; break;
            case eSuspended: stt = "susp";  break;
            case eDeleted:   stt = "del";   break;
            default: break;
        }
        int core = (int)st[i].xCoreID;
        char cbuf[5]; if (core < 0 || core > 1) strcpy(cbuf, "any"); else snprintf(cbuf,5,"%d",core);
        Serial.printf("%-16s %-4s %-4u %-10s %lu\n",
            st[i].pcTaskName, cbuf, (unsigned)st[i].uxCurrentPriority, stt,
            (unsigned long)(st[i].usStackHighWaterMark * sizeof(StackType_t)));
    }
#else
    Serial.println("task table needs configUSE_TRACE_FACILITY; loop stack free:");
    Serial.printf("  %lu B\n", (unsigned long)uxTaskGetStackHighWaterMark(NULL));
#endif
}

static void scan_bus(TwoWire& w, const char* name) {
    int found = 0;
    Serial.printf("%s:", name);
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        w.beginTransmission(a);
        if (w.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
    }
    if (!found) Serial.print(" (none)");
    Serial.println();
}
static void diag_i2cscan() {
    Serial.println("I2C root ACK scan (not chip identity; devices scan traverses configured PaHub):");
    Serial.print("  Internal (m5::In_I2C GPIO13/15):");
    bool internal_ack[128] = {};
    int found = 0;
    for (uint8_t a=8;a<0x78;++a) {
        internal_ack[a] = m5::In_I2C.scanID(a,100000);
        if (internal_ack[a]) { Serial.printf(" 0x%02X",a); ++found; }
    }
    if (!found) Serial.print(" (none)");
    Serial.println();
    scan_bus(Wire1, "  Wire1");
    const uint8_t addr = ioexp_addr();
    if (ioexp_present() && addr >= 8 && addr < 0x78) {
        Serial.printf("IO-exp driver identity: StamPLC-IO; address=0x%02X (cached at init); scan ACK=%s\n",
                      addr, internal_ack[addr] ? "yes" : "no");
    } else {
        Serial.println("IO-exp driver identity: not registered at init; address=unknown");
    }
    Serial.println("ACK is address-level reachability, not chip identity or a valid sensor sample.");
}

static void diag_selftest() {
    int pass = 0, fail = 0;
    #define CHK(cond, name) do { if (cond) { Serial.printf("  PASS  %s\n", name); pass++; } \
                                 else       { Serial.printf("  FAIL  %s\n", name); fail++; } } while (0)
    float t = io.temp_read(), v = io.voltage_read();
    Serial.println("self-test:");
    CHK(esp_get_free_heap_size() > 40000,      "heap > 40KB");
    CHK(uxTaskGetStackHighWaterMark(NULL) > 512,"loop stack headroom");
    CHK(t > -10.0f && t < 90.0f,               "board temp sane (LM75)");
    CHK(v > 3.0f && v < 30.0f,                  "input voltage sane (INA226)");
    CHK(plc_scan_last_us() > 0,                 "PLC scan running");
    Serial.printf("result: %d passed, %d failed\n", pass, fail);
    #undef CHK
}

static void diag_blackbox() {
    Serial.println("[BLACKBOX] (survives a crash reboot; power loss clears it)");
    Serial.printf("  boots=%lu  crashes=%lu  this-reset=%s\n",
        (unsigned long)s_bb.boot_count, (unsigned long)s_bb.crash_count, plc_diag_reset_reason_str());
    Serial.printf("  last checkpoint: uptime=%lus mode=%d scan=%luus(max %lu) faults=%d forces=%d\n",
        (unsigned long)s_bb.last_uptime_s, s_bb.last_mode, (unsigned long)s_bb.last_scan_us,
        (unsigned long)s_bb.last_scan_max_us, s_bb.last_faults, s_bb.last_forces);
    Serial.printf("  min-heap=%lu  last-cmd='%s'  phase=%s\n",
        (unsigned long)s_bb.min_heap, s_bb.last_cmd, s_bb.phase);
}

static void diag_health(bool json) {
    esp_chip_info_t ci; esp_chip_info(&ci);
    const esp_app_desc_t* app = esp_app_get_description();
    char elf_sha[65] = {};
    if (app) for (int i = 0; i < 32; i++) snprintf(elf_sha + 2*i, 3, "%02x", app->app_elf_sha256[i]);
    float temp = io.temp_read(), vin = io.voltage_read(), cur = io.current_read();
    if (json) {
        Serial.print("{\"fw\":\""); Serial.print(app ? app->version : "?");
        Serial.print("\",\"built\":\""); Serial.print(app ? app->date : "?");
        Serial.print("\",\"elf_sha256\":\""); Serial.print(elf_sha);
        Serial.print("\",\"map_sha256\":\""); Serial.print(pointmap::SHA256);
        Serial.print("\",\"map_version\":"); Serial.print(pointmap::VERSION);
        Serial.print(",\"reset\":\""); Serial.print(plc_diag_reset_reason_str());
        Serial.print("\",\"boots\":"); Serial.print(s_bb.boot_count);
        Serial.print(",\"crashes\":"); Serial.print(s_bb.crash_count);
        Serial.print(",\"uptime_s\":"); Serial.print(millis()/1000);
        Serial.print(",\"heap\":"); Serial.print(esp_get_free_heap_size());
        Serial.print(",\"heap_min\":"); Serial.print(esp_get_minimum_free_heap_size());
        Serial.print(",\"loop_stack_free\":"); Serial.print((unsigned)uxTaskGetStackHighWaterMark(NULL));
        Serial.print(",\"cpu_mhz\":"); Serial.print(getCpuFrequencyMhz());
        Serial.print(",\"mode\":\""); Serial.print(plc_mode_str());
        Serial.print("\",\"scan_us\":"); Serial.print(plc_scan_last_us());
        Serial.print(",\"scan_max_us\":"); Serial.print(plc_scan_max_us());
        Serial.print(",\"faults\":"); Serial.print(plc_fault_active_count());
        Serial.print(",\"forces\":"); Serial.print(plc_force_count());
        Serial.print(",\"ioexp\":"); Serial.print(ioexp_present()?1:0);
        Serial.print(",\"temp_c\":"); Serial.print(temp,1);
        Serial.print(",\"vin_v\":"); Serial.print(vin,2);
        Serial.print(",\"cur_a\":"); Serial.print(cur,3);
        Serial.print(",\"mac\":\""); Serial.print(plc_device_mac());
        Serial.print("\",\"seed\":\""); Serial.print(*plc_auth_seed()?plc_auth_seed():"");
        Serial.println("\"}");
    } else {
        Serial.println("==== StamPLC diagnostics ====");
        Serial.printf("firmware   : %s  (built %s)\n", app?app->version:"?", app?app->date:"?");
        Serial.printf("chip       : ESP32-S3 rev%d  %d core  %lu MHz  flash %lu MB\n",
            ci.revision, ci.cores, (unsigned long)getCpuFrequencyMhz(),
            (unsigned long)(ESP.getFlashChipSize()/(1024*1024)));
        Serial.printf("reset      : %s   boots=%lu  crashes=%lu  uptime=%lus\n",
            plc_diag_reset_reason_str(), (unsigned long)s_bb.boot_count,
            (unsigned long)s_bb.crash_count, (unsigned long)(millis()/1000));
        diag_mem();
        Serial.printf("PLC        : mode=%s scan=%luus(max %lu) faults=%d forces=%d ioexp=%s\n",
            plc_mode_str(), (unsigned long)plc_scan_last_us(), (unsigned long)plc_scan_max_us(),
            plc_fault_active_count(), plc_force_count(), ioexp_present()?"online":"absent");
        Serial.printf("sensors    : temp=%.1fC  vin=%.2fV  cur=%.3fA\n", temp, vin, cur);
        Serial.printf("identity   : MAC=%s  seed=%s\n",
            plc_device_mac(), *plc_auth_seed()?plc_auth_seed():"(factory)");
    }
}

// The remote-troubleshooting bundle: ONE copy-pasteable block the student sends
// us. The markers delimit text; there is no checksum or integrity guarantee.
// Contains the health JSON, the black box, tasks and address-level scans.
static void diag_support() {
    Serial.println("-----BEGIN STAMPLC SUPPORT BUNDLE-----");
    Serial.print("health="); diag_health(true);
    diag_blackbox();
    diag_tasks();
    diag_i2cscan();
    Serial.println("-----END STAMPLC SUPPORT BUNDLE-----");
    Serial.println("(copy everything between the BEGIN/END markers and send it to your instructor)");
}

// bounded RAM hex dump: `memdump <hexaddr> [len]` - for when we ask a student to
// read a specific region. Refuses addresses outside internal SRAM and caps length.
static void diag_memdump(char* a1, char* a2) {
    if (!a1) { Serial.println("usage: memdump <hexaddr> [len<=1024]"); return; }
    uintptr_t addr = (uintptr_t)strtoul(a1, nullptr, 16);
    int len = a2 ? atoi(a2) : 64;
    if (len < 1) len = 1; if (len > 1024) len = 1024;
    // ESP32-S3 internal SRAM data range (safe to read):
    const uintptr_t LO = 0x3FC80000u, HI = 0x3FD00000u;
    if (addr < LO || addr + len > HI) {
        Serial.printf("refused: 0x%08lX not in readable SRAM [0x%08lX-0x%08lX]\n",
            (unsigned long)addr, (unsigned long)LO, (unsigned long)HI);
        return;
    }
    const uint8_t* p = (const uint8_t*)addr;
    for (int i = 0; i < len; i += 16) {
        Serial.printf("%08lX ", (unsigned long)(addr + i));
        for (int j = 0; j < 16 && i + j < len; j++) Serial.printf("%02X ", p[i + j]);
        Serial.print(" |");
        for (int j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = p[i + j]; Serial.print((c >= 32 && c < 127) ? (char)c : '.');
        }
        Serial.println("|");
    }
}


static void diag_btntest() {
    Serial.printf("board id (M5.getBoard()) = %d  (board_M5StampPLC is the StamPLC value)\n",
                  (int)M5.getBoard());
    Serial.println("press A / B / C now (10s). columns: raw0x0F  A B C (isPressed)");
    uint8_t lastraw = 0xEE; int lastA=-1,lastB=-1,lastC=-1;
    uint32_t end = millis() + 10000;
    while ((int32_t)(end - millis()) > 0) {
        M5.update();
        uint8_t raw = m5::In_I2C.readRegister8(0x43, 0x0F, 400000);
        int a = M5.BtnA.isPressed(), b = M5.BtnB.isPressed(), c = M5.BtnC.isPressed();
        if (raw != lastraw || a != lastA || b != lastB || c != lastC) {
            Serial.printf("  raw=0x%02X  A=%d B=%d C=%d\n", raw, a, b, c);
            lastraw = raw; lastA = a; lastB = b; lastC = c;
        }
        delay(30);
    }
    Serial.println("btntest done");
}

bool plc_diag_console(const char* cmd, char* a1, char* a2) {
    if (!strcmp(cmd, "diag")) {
        diag_health(a1 && !strcmp(a1, "json"));
        return true;
    }
    if (!strcmp(cmd, "mem"))       { diag_mem();      return true; }
    if (!strcmp(cmd, "tasks"))     { diag_tasks();    return true; }
    if (!strcmp(cmd, "i2cscan"))   { diag_i2cscan();  return true; }
    if (!strcmp(cmd, "selftest"))  { diag_selftest(); return true; }
    if (!strcmp(cmd, "blackbox") || !strcmp(cmd, "bb")) { diag_blackbox(); return true; }
    if (!strcmp(cmd, "why") || !strcmp(cmd, "crash")) {
        Serial.printf("last reset: %s\n", plc_diag_reset_reason_str());
        diag_blackbox(); return true;
    }
    if (!strcmp(cmd, "btntest"))   { diag_btntest();  return true; }
    if (!strcmp(cmd, "support"))   { diag_support();  return true; }
    if (!strcmp(cmd, "memdump"))   { diag_memdump(a1, a2); return true; }
    if (!strcmp(cmd, "reboot"))    { Serial.println("rebooting..."); Serial.flush(); delay(50); esp_restart(); }
    return false;
}

void plc_diag_help() {
    Serial.println(F("  devices status|scan|samples|catalog | devices sampling off|on | devices profile observe|direct-env|pahub70..pahub77"));
    Serial.println(F("  diag [json]          health snapshot (chip, heap, PLC, sensors)"));
    Serial.println(F("  mem | tasks          heap/stack summary | per-task stacks"));
    Serial.println(F("  i2cscan | selftest   probe I2C buses | quick hardware self-test"));
    Serial.println(F("  why | blackbox       last reset reason + crash black box"));
    Serial.println(F("  memdump <hex> [len]  bounded RAM hex dump"));
    Serial.println(F("  support              copy-paste bundle to send us for remote help"));
    Serial.println(F("  reboot               software restart"));
}
