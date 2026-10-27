#include "plc_console.h"
#include "display_capture_console.h"
#include "console_health.h"
#include "device_inventory.h"
#include "svc_config.h"
#include "port_c.h"
#include "eth_net.h"
#include "plc_diag.h"
#include "plc_auth.h"
#include "modbus_serial.h"
#include "modbus_rtu.h"
#include "plc_core.h"
#include "time_sync.h"
#include "serial_config.h"
#include "process_bindings.h"
#include "acquisition_record.h"
#include "diagnostic_log.h"
#include "record_frame.h"
#include "plc_tags.h"
#include "m5stamplc_io.h"
#include "modbus_tcp_server.h"
#include "io_expansion.h"
#include "program_loader.h"
#include "plc_engine.h"
#include <cstring>
#include <cstdlib>
#include <ctype.h>

extern plc::Engine g_engine;
extern plc::Instr  g_prog[32];
extern int         g_prog_n;

static char s_line[3072]; // same bounded capacity as ProgramLoader, including IR
static int  s_len = 0;
static char s_last_eol = 0;
static uint32_t s_last_input_ms = 0;   // millis() of the last received byte
static bool     s_quiet = false;       // 'quiet on' silences periodic telemetry

// Keep generated readback stable while USB drains it across loop visits.
#ifdef OPENPLC_GENERATED_CONTROLLER
static recording::Sender s_runtime_sender;
static char s_runtime_wire[4096];
static bool s_runtime_failed=false;
static bool runtime_busy() { return s_runtime_sender.active(); }
static bool runtime_failed() { return s_runtime_failed; }
static void runtime_poll() {
    if(!runtime_busy())return;
    // Explicit control escape at the head of RX; ordinary commands remain queued.
    // This is a software PROGRAM request, never a hardware emergency stop.
    if (Serial.available() && Serial.peek()==3) {
        Serial.read();
        s_runtime_sender.size=0;s_runtime_failed=true;
        plc_set_mode(PLC_PROGRAM,SRC_USB,"diagnostic cancel");
        return;
    }
    s_runtime_sender.poll(Serial, s_runtime_wire, millis());
    if(s_runtime_sender.failed)s_runtime_failed=true;
}
static void runtime_reply() {
    String json = plc_generated_json();
    if(json.endsWith("}")) {
        json.remove(json.length()-1);
        json += s_runtime_failed ? ",\"console_previous_transfer_failed\":true}" : ",\"console_previous_transfer_failed\":false}";
    }
    size_t n = json.length();
    if (!n || n + 2 > sizeof(s_runtime_wire)) {
        const char* error = s_runtime_failed ? "{\"error\":\"runtime response capacity\",\"console_previous_transfer_failed\":true}" : "{\"error\":\"runtime response capacity\",\"console_previous_transfer_failed\":false}";
        n = strlen(error);
        memcpy(s_runtime_wire+1, error, n);
    } else memcpy(s_runtime_wire+1, json.c_str(), n);
    s_runtime_wire[0]='\n';++n;
    s_runtime_failed=false;
    s_runtime_wire[n++] = '\n';
    s_runtime_sender.start(n, millis());
}
#else
static bool runtime_busy() { return false; }
static bool runtime_failed() { return false; }
static void runtime_poll() {}
#endif

// Loop-owned periodic telemetry is retained unless muted. Worker diagnostics
// always use the bounded logger; they never consult loop-owned console state.
bool console_telemetry_muted() {
    return acquisition_record_busy() || diagnostic_log_console_busy() || runtime_busy() || display_capture_console_busy() || s_quiet || (s_last_input_ms != 0 && (uint32_t)(millis() - s_last_input_ms) < 4000);
}

static int32_t tag_value(const TagDef* t) {
    switch (t->kind) {
        case TK_COIL: return plc_coil_live(t->addr) ? 1 : 0;
        case TK_DI:   return plc_di_live((uint8_t)t->addr) ? 1 : 0;
        default:      return plc_tag_decode(t, modbus_server.get_register(t->addr));
    }
}

static void print_point(const TagDef* t) {
    int32_t v = tag_value(t);
    Serial.printf("  %-17s = %-6ld %-4s", t->name, (long)v, t->unit[0] ? t->unit : "");
    if (t->kind == TK_COIL && plc_coil_is_forced(t->addr))
        Serial.printf(" [FORCED %d]", plc_coil_forced_val(t->addr) ? 1 : 0);
    if (t->kind == TK_DI && plc_di_is_forced((uint8_t)t->addr))
        Serial.printf(" [FORCED %d]", plc_di_forced_val((uint8_t)t->addr) ? 1 : 0);
    Serial.println();
}

static void cmd_help() {
    Serial.println(F("commands:"));
#ifdef OPENPLC_GENERATED_CONTROLLER
    Serial.println(F("  Ctrl-C during runtime reply: cancel and request PROGRAM (not an emergency stop)"));
#endif
    Serial.println(F("  status               CPU mode, scan time, faults, forces, comms"));
    Serial.println(F("  points               list every named point and live value"));
    Serial.println(F("  show <tag>           one point in detail"));
    Serial.println(F("  io                   digital inputs and relay commanded/actual"));
    Serial.println(F("  set <tag> <val>      write a writable point (logged source=USB)"));
    Serial.println(F("  force <tag> <0|1>    override a coil/DI in the I/O image"));
    Serial.println(F("  unforce <tag>        release a force"));
    Serial.println(F("  forces               list active forces / clearforces"));
    Serial.println(F("  mode run|prog|remote key switch: RUN locks download, PROGRAM=safe+download, REMOTE=remote"));
    Serial.println(F("  faults | clrflt      show / clear the fault table"));
    Serial.println(F("  log                  the source-attributed audit log"));
    Serial.println(F("  logs status|read N  retained diagnostics with sequence/loss counters"));
    Serial.println(F("  logs exercise 1..32 generate marked task logs (PROGRAM only)"));
    Serial.println(F("  mon <tag> [n]        stream a point n times (default 10)"));
    Serial.println(F("  cwe                  embedded weaknesses & service ports"));
    Serial.println(F("  clk                  show the wall-clock (stored as 32-bit - Y2038 target)"));
    Serial.println(F("  ntpsync [ip] [port] | auto [ip] [sec] | off   sync time from the net (unauth)"));
    Serial.println(F("  netcfg [json] | dhcp | static <ip> <gw> <mask> | ntp <ip> [port] | auth <u> <p> | save"));
    Serial.println(F("  bustrace on|off      retain decoded bus diagnostics (see logs)"));
    Serial.println(F("  rtudiag              RTU worker counters and longest poll gap (us)"));
    Serial.println(F("  portc [status|profile disabled|hx711|h2-uart|baud N|read|send HEX]"));
    Serial.println(F("  proto [name on|off]  selectable protocols/services + attack surface"));
    Serial.println(F("  quiet on|off         mute/resume loop telemetry retention"));
    Serial.println(F("  console              how this USB port works (for humans and tools)"));
    Serial.println(F("  sockets [timeout <s>] W5500 socket pool + set the half-open age-out"));
    Serial.println(F("  record <32hex> <n>  framed acquisition source n (0..31), read-only"));
    Serial.println(F("  ver | help"));
    Serial.println(F("debug/diagnostics (shipped - for you and remote support):"));
    plc_diag_help();
}

// Usage guidance for whoever (or whatever) attaches to this port.
static void cmd_console() {
    Serial.println(F("this port = ESP32-S3 USB-Serial/JTAG CDC, 115200 8N1."));
    Serial.println(F("line proto : send a command then Enter. LF or CR-LF both end a line."));
    Serial.println(F("             backspace/DEL edit the line; printable input echoes back."));
    Serial.println(F("prompt     : the literal string \"plc> \" means ready-for-input - a tool"));
    Serial.println(F("             can wait for it to stay in sync with command replies."));
    Serial.println(F("telemetry  : retained diagnostics are read with logs status/read N."));
    Serial.println(F("             quiet on/off controls loop telemetry retention."));
    Serial.println(F("writes     : set/force need no login here (physical-port trust) and are"));
    Serial.println(F("             recorded in the audit log as source=USB - see \"log\"."));
    Serial.println(F("connecting : screen /dev/tty.usbmodemXXXX 115200   or   pio device monitor."));
    Serial.println(F("             open the port and request commands. Do NOT drive"));
    Serial.println(F("             esptool-style DTR/RTS reset lines to \"wake\" it - that reboots"));
    Serial.println(F("             the CPU and drops every live service."));
    Serial.println(F("network alt: the HTTP JSON API on :8080 (/api/points /api/mode /api/set"));
    Serial.println(F("             /api/log) is the scriptable path that avoids USB line state."));
}

static void cmd_sockets(char* a1, char* a2) {
    if (a1 && !strcmp(a1, "timeout")) {
        if (a2) {
            uint32_t s = (uint32_t)strtoul(a2, nullptr, 10);
            svc_set_sock_timeout_s(s);
            eth_set_sock_timeout_ms(s * 1000);
            Serial.printf("socket age-out = %lus (persisted)%s\n",
                (unsigned long)s, s == 0 ? "  [reaper DISABLED]" : "");
        } else {
            Serial.println("usage: sockets timeout <seconds>   (0 disables the reaper)");
        }
        return;
    }
    Serial.printf("W5500 socket pool: 8 total, %d in LISTEN, age-out %lus, reaped %lu since boot\n",
        eth_listen_count(), (unsigned long)(eth_get_sock_timeout_ms() / 1000),
        (unsigned long)eth_reaped_count());
    eth_dump_sockets();
}

static void cmd_status() {
    Serial.printf("MODE=%s  scan last=%luus min=%luus max=%luus  scans=%lu  hb=%lu\n",
        plc_mode_str(), (unsigned long)plc_scan_last_us(), (unsigned long)plc_scan_min_us(),
        (unsigned long)plc_scan_max_us(), (unsigned long)plc_scan_count(), (unsigned long)plc_heartbeat());
    Serial.printf("faults=%d  forces=%d (active=%s)  IO-exp=%s  uptime=%lus\n",
        plc_fault_active_count(), plc_force_count(), plc_forces_active() ? "YES" : "no",
        ioexp_present() ? "online" : "absent", (unsigned long)(millis() / 1000));
}

static void cmd_points() {
    for (int i = 0; i < PLC_TAG_COUNT; i++) print_point(&PLC_TAGS[i]);
}

static void cmd_io() {
    Serial.print("DI  :");
    for (int i = 0; i < 8; i++) Serial.printf(" %d%s", plc_di_live(i) ? 1 : 0, plc_di_is_forced(i) ? "F" : "");
    Serial.println();
    Serial.println("relay commanded/actual (base 0-3):");
    for (int k = 0; k < 4; k++)
        Serial.printf("  relay%d cmd=%d act=%d%s\n", k, plc_relay_commanded(k) ? 1 : 0,
            plc_relay_actual(k) ? 1 : 0, plc_relay_commanded(k) != plc_relay_actual(k) ? "  MISMATCH" : "");
    uint8_t expansion = 0;
    if (ioexp_output_readback(&expansion))
        Serial.printf("expansion register readback mech=%u ssr0=%u ssr1=%u (not contact feedback)\n",
            (expansion >> 4) & 1, expansion & 1, (expansion >> 1) & 1);
    else Serial.println("expansion register readback unavailable");
}

static void cmd_set(char* tagn, char* valn) {
    const TagDef* t = plc_tag_find(tagn);
    if (!t) { Serial.println("? unknown tag"); return; }
    if (!t->writable) { Serial.println("? not writable (use force for inputs)"); return; }
    long v = strtol(valn, nullptr, 0);
    if (t->kind == TK_COIL) {
        modbus_server.set_coil(t->addr, v != 0);
        plc_log(SRC_USB, "", "SET %s(coil %u) = %d", t->name, t->addr, v ? 1 : 0);
        Serial.printf("ok %s = %d\n", t->name, v ? 1 : 0);
    } else {
        modbus_server.set_register(t->addr, (uint16_t)v);
        plc_log(SRC_USB, "", "SET %s(reg %u) = %ld", t->name, t->addr, v);
        Serial.printf("ok %s = %ld\n", t->name, v);
    }
}

static bool force_by_addr(const char* tagn, bool on, bool v) {
    // Accept %IX0 / IX0 / %QX3 / QX3 so a UI can force by address, not tag name.
    const char* p = tagn; if (*p == '%') p++;
    char t0 = (char)toupper(p[0]), t1 = (char)toupper(p[1]);
    if (t1 != 'X' || (t0 != 'I' && t0 != 'Q')) return false;
    int idx = atoi(p + 2);
    if (t0 == 'I') {                                  // %IX digital input
        if (idx < 0 || idx > 7) { Serial.println("? IX0-7"); return true; }
        if (on) plc_force_di((uint8_t)idx, v, SRC_USB, ""); else plc_unforce_di((uint8_t)idx, SRC_USB, "");
        Serial.printf("%sforced %%IX%d%s\n", on ? "" : "un", idx, on ? (v ? " = 1" : " = 0") : "");
    } else {                                          // %QX relay/coil
        if (idx < 0 || idx > 3) { Serial.println("? QX0-3"); return true; }
        // QX is zero-based relay numbering; protocol coils start at MOTOR.
        const uint16_t address = pointmap::RELAY0 + (uint16_t)idx;
        if (on) plc_force_coil(address, v, SRC_USB, ""); else plc_unforce_coil(address, SRC_USB, "");
        Serial.printf("%sforced %%QX%d%s\n", on ? "" : "un", idx, on ? (v ? " = 1" : " = 0") : "");
    }
    return true;
}

static void cmd_force(char* tagn, char* valn, bool on) {
    bool v = valn && strtol(valn, nullptr, 0) != 0;
    if (force_by_addr(tagn, on, v)) return;           // %IX0 / %QX3 address form
    const TagDef* t = plc_tag_find(tagn);
    if (!t) { Serial.println("? unknown tag"); return; }
    if (!t->forceable) { Serial.println("? not forceable"); return; }
    if (on) {
        if (t->kind == TK_COIL) plc_force_coil(t->addr, v, SRC_USB, "");
        else if (t->kind == TK_DI) plc_force_di((uint8_t)t->addr, v, SRC_USB, "");
        Serial.printf("forced %s = %d\n", t->name, v ? 1 : 0);
    } else {
        if (t->kind == TK_COIL) plc_unforce_coil(t->addr, SRC_USB, "");
        else if (t->kind == TK_DI) plc_unforce_di((uint8_t)t->addr, SRC_USB, "");
        Serial.printf("unforced %s\n", t->name);
    }
}

static void cmd_forces() {
    if (!plc_forces_active()) { Serial.println("no active forces"); return; }
    Serial.println("FORCES ACTIVE:");
    for (int i = 0; i < PLC_TAG_COUNT; i++) {
        const TagDef* t = &PLC_TAGS[i];
        if (t->kind == TK_COIL && plc_coil_is_forced(t->addr))
            Serial.printf("  %s = %d\n", t->name, plc_coil_forced_val(t->addr) ? 1 : 0);
        if (t->kind == TK_DI && plc_di_is_forced((uint8_t)t->addr))
            Serial.printf("  %s = %d\n", t->name, plc_di_forced_val((uint8_t)t->addr) ? 1 : 0);
    }
}

static void cmd_faults() {
    FaultEntry f[16]; int n = plc_fault_list(f, 16);
    if (n == 0) { Serial.println("no faults"); return; }
    for (int i = 0; i < n; i++)
        Serial.printf("  [%s] code=%u @%lums: %s\n", f[i].active ? "ACTIVE" : "clear",
            f[i].code, (unsigned long)f[i].t_ms, f[i].msg);
}

static void cmd_log() {
    int n = plc_log_count();
    if (n == 0) { Serial.println("(log empty)"); return; }
    char buf[112];
    for (int i = n - 1; i >= 0; i--) if (plc_log_get(i, buf, sizeof(buf))) Serial.printf("  %s\n", buf);
}

static void cmd_mon(char* tagn, char* cntn) {
    const TagDef* t = plc_tag_find(tagn);
    if (!t) { Serial.println("? unknown tag"); return; }
    int cnt = cntn ? atoi(cntn) : 10; if (cnt < 1) cnt = 1; if (cnt > 100) cnt = 100;
    for (int i = 0; i < cnt; i++) { Serial.printf("  %s = %ld %s\n", t->name, (long)tag_value(t), t->unit); delay(200); }
}

static void cmd_login(char* user, char* pass) {
    if (!user || !pass) { Serial.println("usage: login <user> <pass>   (users: operator, service, admin)"); return; }
    // [CWE-319] over IP this same login travels in cleartext; [CWE-307] no lockout.
    if (plc_auth_login(user, pass)) {
        Serial.printf("[AUTH] logged in as %s. token=%s\n", plc_auth_whoami(), plc_auth_issue_token().c_str());
    } else {
        Serial.printf("[AUTH] login failed (attempt #%lu - no lockout, keep trying)\n",
                      (unsigned long)plc_auth_failed_count());
    }
}

static void cmd_passwd(char* np) {
    if (!plc_authed()) { Serial.println("[AUTH] not logged in - 'login operator <pass>' first"); return; }
    if (!np) { Serial.println("usage: passwd <new>   (policy: max 8 chars, A-Z 0-9 only)"); return; }
    String msg;
    if (plc_auth_set_user_password(np, msg)) Serial.printf("[AUTH] %s\n", msg.c_str());
    else Serial.printf("[AUTH] rejected: %s\n", msg.c_str());   // [CWE-521] strong pw refused here
}

static void cmd_svcinfo() {
    // [CWE-200/1273] info exposure: the MAC (and hence the service pw input) is free to read.
    const char* sd = plc_auth_seed();
    Serial.printf("seed       : %s%s\n", (sd && *sd) ? sd : "(none)",
                  (sd && *sd) ? "  -> per-student synthetic MAC" : "  -> factory MAC");
    Serial.printf("device MAC : %s\n", plc_device_mac());
    Serial.println("service pw : SRV-XXXXXX derived from the last 3 MAC octets (XOR 0x2A)");
    Serial.printf("user pw     : %s  [CWE-257 recoverable / CWE-256 plaintext]\n", plc_auth_user_password());
    Serial.printf("session     : %s   failed-logins=%lu (no lockout)\n",
                  plc_auth_whoami(), (unsigned long)plc_auth_failed_count());
}

static void cmd_cwe() {
    Serial.println("Embedded (intentional) weaknesses:");
    Serial.println("  CWE-798 hardcoded creds admin:openplc   CWE-319 cleartext Modbus/502");
    Serial.println("  CWE-306 unauth estop coil write         CWE-494 unsigned OTA");
    Serial.println("  CWE-78  cmd injection (diag ping)        CWE-489 unauth telnet/23");
    Serial.println("  CWE-330 predictable session token        CWE-22  path traversal (log viewer)");
    Serial.println("  CWE-521 user pw max8 A-Z0-9 (weak policy)  CWE-256 user pw plaintext at rest");
    Serial.println("  CWE-798 service pw derived from device MAC  CWE-307 no login lockout");
    Serial.println("  CWE-200 MAC exposed in banners (enables service-pw derivation)");
    Serial.println("  CWE-197 32-bit wall-clock truncation (Y2038)  CWE-306/345 unauth+unverified SNTP time (AiTM)");
    Serial.println("  try: 'svcinfo', then 'login service SRV-...', 'login operator 1234', 'passwd ...'");
    Serial.println("  try: 'clk', then 'ntpsync <ip>' - an on-path time server can step this clock to 1902");
    Serial.println("  ports: Modbus 502, HMI 8080, telnet 23, OTA 80, SNTP 123 (client/outbound)");
}

// ---- wall-clock readout + network time sync (NTP AiTM / Y2038 teaching target) ----
static void cmd_clk() {
    if (!time_sync_valid()) {
        Serial.printf("clock: UNSYNCED  (uptime %lus; sync with 'ntpsync <server> [port]')\n",
                      (unsigned long)(millis() / 1000));
        return;
    }
    int64_t now = time_sync_now();
    char buf[40]; time_sync_format(now, buf, sizeof(buf));
    bool wrapped = (now < 946684800);   // before year 2000 => the signed 32-bit field overflowed
#ifdef ENABLE_CWE_DEMOS
    Serial.printf("clock: %s   %s\n", buf,
                  wrapped ? "<-- WRAPPED! 32-bit overflow (CWE-197/Y2038)"
                          : "[stored signed int32 - fits, no truncation yet]");
#else
    (void)wrapped;
    Serial.printf("clock: %s   [stored as 64-bit epoch - Y2038-safe]\n", buf);
#endif
    Serial.printf("  source: %s:%u%s\n", time_sync_server(), time_sync_port(),
                  time_sync_interval_ms() ? "  (auto-sync ON)" : "");
}

static void cmd_ntpsync(const char* a1, const char* a2, const char* a3) {
    if (a1 && !strcmp(a1, "off")) { time_sync_set_server(nullptr, 0, 0); Serial.println("auto-sync OFF"); return; }
    if (a1 && !strcmp(a1, "auto")) {
        const char* ip = (a2 && a2[0]) ? a2 : time_sync_server();
        uint32_t sec = a3 ? (uint32_t)atoi(a3) : 10;
        if (!ip || !ip[0]) { Serial.println("no server - give an ip or configure a gateway"); return; }
        time_sync_set_server(ip, time_sync_port(), sec * 1000);
        Serial.printf("auto-sync ON: %s:%u every %lus (an on-path server now steers this clock)\n",
                      ip, time_sync_port(), (unsigned long)sec);
        return;
    }
    const char* ip = (a1 && a1[0]) ? a1 : time_sync_server();
    uint16_t port = (a2 && a2[0]) ? (uint16_t)atoi(a2) : time_sync_port();
    if (!ip || !ip[0]) { Serial.println("usage: ntpsync <ip> [port]   (no gateway configured)"); return; }
    Serial.printf("querying time from %s:%u (via the comms core) ...\n", ip, port);
    // Post the request; the comms core (core 0) does the W5500 I/O + applies it.
    // We only wait for the result here - no network ops on the console/scan core.
    uint32_t seq = time_sync_request(ip, port);
    int32_t u32 = 0; int64_t u64 = 0; bool ok = false, tcp = false; bool got = false;
    uint32_t t0 = millis();
    while (millis() - t0 < 4500) {
        if (time_sync_result(seq, &u32, &u64, &ok, &tcp)) { got = true; break; }
        delay(20);   // yields to the RTOS; feeds the WDT; never touches the W5500
    }
    if (!got)  { Serial.println("  no result yet (comms core busy) - run 'clk' shortly"); return; }
    if (!ok)   { Serial.println("  no reply (time server down, or UDP+TCP both unreachable)"); return; }
    Serial.printf("  reply over %s\n", tcp ? "TCP (fallback)" : "UDP");
#ifdef ENABLE_CWE_DEMOS
    if (u64 != (int64_t)u32) {          // the 32-bit field could not hold the real time
        char real[40], stored[40];
        time_t t64 = (time_t)u64; struct tm tmv; gmtime_r(&t64, &tmv);
        strftime(real, sizeof(real), "%Y-%m-%d %H:%M:%S UTC", &tmv);
        time_sync_format((int64_t)u32, stored, sizeof(stored));
        Serial.printf("  on the wire: %s  (64-bit, intact)\n", real);
        Serial.printf("  STORED     : %s  <-- 32-bit TRUNCATION, clock WRAPPED (CWE-197/Y2038)\n", stored);
    } else {
        char buf[40]; time_sync_format((int64_t)u32, buf, sizeof(buf));
        Serial.printf("  clock      : %s   (no truncation - time fits 32 bits)\n", buf);
    }
#else
    (void)u32;                           // hardened: the full 64-bit time is stored, intact
    char real[40]; time_t t64 = (time_t)u64; struct tm tmv; gmtime_r(&t64, &tmv);
    strftime(real, sizeof(real), "%Y-%m-%d %H:%M:%S UTC", &tmv);
    Serial.printf("  clock      : %s   (64-bit store - no Y2038 wrap)\n", real);
#endif
    // the comms core already applied the clock and wrote the audit log entry.
}

// ---- network config (DHCP/static/IP/NTP + remote-config creds) ----
// Same NVS the boot serial menu writes; also the back-end for the Logic Studio
// config page (over USB) and the authenticated /api/config endpoint (over the net).
static void cmd_netcfg(const char* a1, const char* a2) {
    char* a3 = strtok(nullptr, " \t");
    char* a4 = strtok(nullptr, " \t");
    if (!a1 || !strcmp(a1, "json")) {
        if (a1 && !strcmp(a1, "json")) {
            Serial.printf("{\"mode\":\"%s\",\"ip\":\"%s\",\"gw\":\"%s\",\"mask\":\"%s\",\"ntp\":\"%s\","
                          "\"ntp_port\":%d,\"cfg_user\":\"%s\",\"link\":\"%s\"}\n",
                g_cfg.use_static ? "static" : "dhcp", g_cfg.ip, g_cfg.gw, g_cfg.mask,
                g_cfg.ntp[0] ? g_cfg.ntp : g_cfg.gw, g_cfg.ntp_port, g_cfg.cfg_user,
                eth_ok() ? eth_ip().c_str() : "no-net");
        } else {
            Serial.printf("net : %s  ip=%s gw=%s mask=%s\n",
                          g_cfg.use_static ? "static" : "dhcp", g_cfg.ip, g_cfg.gw, g_cfg.mask);
            Serial.printf("  live ip : %s\n", eth_ok() ? eth_ip().c_str() : "no-net");
            Serial.printf("  ntp     : %s:%d\n", g_cfg.ntp[0] ? g_cfg.ntp : g_cfg.gw, g_cfg.ntp_port);
            Serial.printf("  cfg auth: %s:%s  [CWE-798/1392 default - change it!]\n", g_cfg.cfg_user, g_cfg.cfg_pass);
            Serial.println("  set: netcfg dhcp | static <ip> <gw> <mask> | ntp <ip> [port] | auth <user> <pass> | save");
            Serial.println("  (changes persist on 'save'; network addressing applies on reboot)");
        }
        return;
    }
    if (!strcmp(a1, "dhcp"))   { g_cfg.use_static = 0; Serial.println("mode=dhcp (save + reboot to apply)"); return; }
    if (!strcmp(a1, "static")) {
        if (!a2 || !a3 || !a4) { Serial.println("usage: netcfg static <ip> <gw> <mask>"); return; }
        g_cfg.use_static = 1;
        strncpy(g_cfg.ip, a2, sizeof(g_cfg.ip) - 1);  g_cfg.ip[sizeof(g_cfg.ip)-1]=0;
        strncpy(g_cfg.gw, a3, sizeof(g_cfg.gw) - 1);  g_cfg.gw[sizeof(g_cfg.gw)-1]=0;
        strncpy(g_cfg.mask, a4, sizeof(g_cfg.mask) - 1); g_cfg.mask[sizeof(g_cfg.mask)-1]=0;
        Serial.printf("mode=static ip=%s gw=%s mask=%s (save + reboot)\n", g_cfg.ip, g_cfg.gw, g_cfg.mask); return;
    }
    if (!strcmp(a1, "ntp")) {
        if (!a2) { Serial.println("usage: netcfg ntp <ip> [port]"); return; }
        strncpy(g_cfg.ntp, a2, sizeof(g_cfg.ntp) - 1); g_cfg.ntp[sizeof(g_cfg.ntp)-1]=0;
        g_cfg.ntp_port = a3 ? atoi(a3) : 123;
        time_sync_set_server(g_cfg.ntp, (uint16_t)g_cfg.ntp_port, time_sync_interval_ms());  // live now
        Serial.printf("ntp=%s:%d (live; 'save' to persist)\n", g_cfg.ntp, g_cfg.ntp_port); return;
    }
    if (!strcmp(a1, "auth")) {
        if (!a2 || !a3) { Serial.println("usage: netcfg auth <user> <pass>"); return; }
        strncpy(g_cfg.cfg_user, a2, sizeof(g_cfg.cfg_user) - 1); g_cfg.cfg_user[sizeof(g_cfg.cfg_user)-1]=0;
        strncpy(g_cfg.cfg_pass, a3, sizeof(g_cfg.cfg_pass) - 1); g_cfg.cfg_pass[sizeof(g_cfg.cfg_pass)-1]=0;
        Serial.println("remote-config creds changed ('save' to persist) - good: the DEFAULT was the weakness"); return;
    }
    if (!strcmp(a1, "save")) { serial_config_save(); plc_log(SRC_USB, "", "netcfg saved"); return; }
    Serial.println("netcfg: json | dhcp | static <ip> <gw> <mask> | ntp <ip> [port] | auth <user> <pass> | save");
}

static void cmd_pjson() {
    Serial.print("{\"mode\":\""); Serial.print(plc_mode_str());
    Serial.print("\",\"scan\":"); Serial.print(plc_scan_last_us());
    Serial.print(",\"scanmax\":"); Serial.print(plc_scan_max_us());
    Serial.print(",\"faults\":"); Serial.print(plc_fault_active_count());
    Serial.print(",\"forces\":"); Serial.print(plc_force_count());
    Serial.print(",\"hb\":"); Serial.print(plc_heartbeat());
    Serial.print(",\"map_version\":"); Serial.print(pointmap::VERSION);
    Serial.print(",\"map_sha256\":\""); Serial.print(pointmap::SHA256);
    Serial.print("\",\"pts\":[");
    for (int i = 0; i < PLC_TAG_COUNT; i++) {
        const TagDef* t = &PLC_TAGS[i];
        int32_t val; bool forced=false, fv=false;
        if (t->kind==TK_COIL){ val=plc_coil_live(t->addr)?1:0; forced=plc_coil_is_forced(t->addr); fv=plc_coil_forced_val(t->addr); }
        else if (t->kind==TK_DI){ val=plc_di_live((uint8_t)t->addr)?1:0; forced=plc_di_is_forced((uint8_t)t->addr); fv=plc_di_forced_val((uint8_t)t->addr); }
        else val=plc_tag_decode(t, modbus_server.get_register(t->addr));
        if (i) Serial.print(",");
        Serial.print("{\"n\":\""); Serial.print(t->name);
        Serial.print("\",\"k\":"); Serial.print((int)t->kind);
        Serial.print(",\"v\":"); Serial.print(val);
        Serial.print(",\"u\":\""); Serial.print(t->unit);
        Serial.print("\",\"w\":"); Serial.print(t->writable?1:0);
        Serial.print(",\"c\":"); Serial.print(t->forceable?1:0);
        Serial.print(",\"f\":"); Serial.print(forced?1:0);
        Serial.print(",\"fv\":"); Serial.print(fv?1:0);
        Serial.print(",\"d\":\""); Serial.print(t->desc); Serial.print("\"}");
    }
    Serial.print("]");
    Serial.print(",\"fb\":[");
    { bool ffirst = true;
      for (int i = 0; i < plc::NINST; i++) {
        if (g_engine.fbrunning(i) || g_engine.fbq(i)) {
          if (!ffirst) Serial.print(","); ffirst = false;
          uint32_t et = g_engine.fbrunning(i) ? (millis() - g_engine.fbt0(i)) : 0;
          Serial.print("{\"i\":"); Serial.print(i);
          Serial.print(",\"q\":"); Serial.print(g_engine.fbq(i) ? 1 : 0);
          Serial.print(",\"et\":"); Serial.print(et); Serial.print("}");
        }
      }
    }
    Serial.println("]}");
}

static void cmd_proto(char* a1, char* a2) {
    if (!a1) {
        Serial.printf("== SOCKET BUDGET: max %d network services  (in use: %d/%d)  -- a 5th is refused ==\n",
                      SVC_MAX_NET, svc_net_enabled_count(), SVC_MAX_NET);
        Serial.println("service      group       port  via  state impl     surface");
        for (int i = 0; i < SVC_COUNT; i++) { const SvcDef& d = SVC_DEFS[i];
            Serial.printf("  %-11s %-11s %5u   %c   %-4s  %-8s %s\n", d.name, d.group, d.port, d.delivery,
                svc_enabled(i) ? "ON" : "off", d.implemented ? "ready" : "planned", d.note); }
        Serial.println("  proto <name> on|off   (reboot to apply)  |  proto json");
        return;
    }
    if (!strcmp(a1, "json")) {
        Serial.print("{\"svc\":[");
        for (int i = 0; i < SVC_COUNT; i++) { const SvcDef& d = SVC_DEFS[i]; if (i) Serial.print(",");
            char dv[2] = { d.delivery, 0 };
            Serial.printf("{\"n\":\"%s\",\"g\":\"%s\",\"p\":%u,\"via\":\"%s\",\"on\":%d,\"impl\":%d,\"note\":\"%s\"}",
                d.name, d.group, d.port, dv, svc_enabled(i) ? 1 : 0, d.implemented ? 1 : 0, d.note); }
        Serial.printf("],\"netmax\":%d,\"neton\":%d}\n", SVC_MAX_NET, svc_net_enabled_count());
        return;
    }
    int idx = svc_find(a1);
    if (idx < 0) { Serial.println("? unknown service (try 'proto')"); return; }
    if (!a2) { Serial.printf("%s is %s\n", SVC_DEFS[idx].name, svc_enabled(idx) ? "ON" : "off"); return; }
    bool on = !strcmp(a2, "on");
    if (!svc_set(idx, on)) {
        Serial.printf("[PROTO] refused: socket budget full (%d/%d network services on). Turn one off first.\n",
                      svc_net_enabled_count(), SVC_MAX_NET);
        return;
    }
    plc_log(SRC_USB, "", "proto %s -> %s", SVC_DEFS[idx].name, on ? "ON" : "off");
    Serial.printf("[PROTO] %s -> %s (reboot to apply)\n", SVC_DEFS[idx].name, on ? "ON" : "off");
    if (on && !SVC_DEFS[idx].implemented) Serial.println("  note: stack not implemented yet - flag stored, no port opens");
}

static void process(char* line) {
    if(display_capture_console_handle(line))return;
#ifdef OPENPLC_GENERATED_CONTROLLER
    if(!strcmp(line,"program") || !strcmp(line,"runtime json")) {runtime_reply();return;}
    if(!strncmp(line,"PLCLOAD",7)) {Serial.println("[LOADER] ERR compiled OpenPLC profile requires firmware artifact");return;}
#endif
    // program-upload frames go to the loader
    if (strncmp(line, "PLCLOAD", 7) == 0) {
        if (plc::program_loader.feed_line(line, g_engine, g_prog, g_prog_n))
            Serial.println("[LOADER] applied");
        return;
    }
    if (!strncmp(line,"bindings",8) && (!line[8] || line[8]==' ' || line[8]=='\t')) {
        plc_diag_note_cmd("bindings");plc_bindings_console(line+8);return;
    }
    char* cmd = strtok(line, " \t");
    if (!cmd) return;
    plc_diag_note_cmd(cmd);
    char* a1 = strtok(nullptr, " \t");
    char* a2 = strtok(nullptr, " \t");
    if (!strcmp(cmd, "record")) {
        if (strtok(nullptr," \t") || !acquisition_record_begin(a1,a2))
            Serial.println("{\"record\":\"refused\",\"reason\":\"record <32 lowercase hex request ID> <source index 0..31>; source must exist\"}");
        return;
    }
    if (!strcmp(cmd, "devices")) {
        device_inventory_console(a1, a2, strtok(nullptr, " \t") != nullptr);
        return;
    }
    if (!strcmp(cmd, "portc")) {
        port_c_console(a1, a2, strtok(nullptr, " \t") != nullptr);
        return;
    }
    if      (!strcmp(cmd, "help") || !strcmp(cmd, "?")) cmd_help();
    else if (!strcmp(cmd, "status")) cmd_status();
    else if (!strcmp(cmd, "points")) cmd_points();
    else if (!strcmp(cmd, "show") && a1) { const TagDef* t = plc_tag_find(a1); if (t) { print_point(t); Serial.printf("    %s\n", t->desc); } else Serial.println("? unknown tag"); }
    else if (!strcmp(cmd, "io")) cmd_io();
    else if (!strcmp(cmd, "program")) {
        Serial.printf("[PROGRAM] instr=%d mode=%s validation=%s scan_ms=%u\n",
            g_prog_n, plc_mode_str(), plc::validate_program(g_prog, g_prog_n) ? "invalid" : "valid",
            OPENPLC_SCAN_CYCLE_MS);
        for (int i = 0; i < g_prog_n && i < 32; i++)
            Serial.printf("[PROGRAM] D %d %u %ld %ld\n", i, g_prog[i].op, (long)g_prog[i].a, (long)g_prog[i].b);
        Serial.println("[PROGRAM] END");
    }
    else if (!strcmp(cmd, "set") && a1 && a2) cmd_set(a1, a2);
    else if (!strcmp(cmd, "force") && a1) cmd_force(a1, a2, true);
    else if (!strcmp(cmd, "unforce") && a1) cmd_force(a1, nullptr, false);
    else if (!strcmp(cmd, "forces")) cmd_forces();
    else if (!strcmp(cmd, "clearforces")) { plc_clear_all_forces(SRC_USB, ""); Serial.println("all forces cleared"); }
    else if (!strcmp(cmd, "mode") && a1) {
        if (!strcmp(a1, "run")) { plc_set_mode(PLC_RUN, SRC_USB, ""); Serial.printf("mode %s (RUN requires valid program, ready inputs and no faults)\n", plc_mode_str()); }
        else if (!strcmp(a1, "prog")) { plc_set_mode(PLC_PROGRAM, SRC_USB, ""); Serial.printf("mode %s (outputs inhibited, download enabled)\n", plc_mode_str()); }
        else if (!strcmp(a1, "remote")) { plc_set_mode(PLC_REMOTE, SRC_USB, ""); Serial.printf("mode %s (REMOTE requires valid program, ready inputs and no faults)\n", plc_mode_str()); }
        else Serial.println("? mode run|prog|remote");
    }
    else if (!strcmp(cmd, "faults")) cmd_faults();
    else if (!strcmp(cmd, "clrflt")) { plc_fault_clear_all(SRC_USB, ""); Serial.println("faults cleared"); }
    else if (!strcmp(cmd, "log")) cmd_log();
    else if (!strcmp(cmd, "mon") && a1) cmd_mon(a1, a2);
    else if (!strcmp(cmd, "pjson")) cmd_pjson();
    else if (!strcmp(cmd, "proto")) cmd_proto(a1, a2);
    else if (!strcmp(cmd, "rs485tx")) modbus_rtu_test_tx();
    else if (!strcmp(cmd, "rtudiag")) modbus_rtu_diag();
    else if (!strcmp(cmd,"logs")) diagnostic_log_console(a1,a2,strtok(nullptr," \t")!=nullptr);
    else if (!strcmp(cmd, "bustrace")) { g_bustrace = a1 && !strcmp(a1, "on"); Serial.printf("bus trace %s\n", g_bustrace ? "ON" : "OFF"); }
    else if (!strcmp(cmd, "cwe")) cmd_cwe();
    else if (!strcmp(cmd, "clk")) cmd_clk();
    else if (!strcmp(cmd, "ntpsync")) cmd_ntpsync(a1, a2, strtok(nullptr, " \t"));
    else if (!strcmp(cmd, "netcfg")) cmd_netcfg(a1, a2);
    else if (!strcmp(cmd, "ver")) Serial.println("StamPLC teaching firmware - PLC core v2 (mode/force/audit/console)");
    else if (!strcmp(cmd, "quiet")) { s_quiet = (a1 && !strcmp(a1, "on")); Serial.printf("telemetry %s\n", s_quiet ? "MUTED (quiet on)" : "streaming (quiet off)"); }
    else if (!strcmp(cmd, "console")) cmd_console();
    else if (!strcmp(cmd, "sockets")) cmd_sockets(a1, a2);
    else if (!strcmp(cmd, "login")) cmd_login(a1, a2);
    else if (!strcmp(cmd, "logout")) { plc_auth_logout(); Serial.println("[AUTH] logged out"); }
    else if (!strcmp(cmd, "whoami")) Serial.printf("%s\n", plc_auth_whoami());
    else if (!strcmp(cmd, "passwd")) cmd_passwd(a1);
    else if (!strcmp(cmd, "gateway")) {
        Serial.println("[GW] USB serial -> Modbus/TCP gateway mode. Text console silenced; reset to exit.");
        Serial.flush();
        modbus_serial_enter();
    }
    else if (!strcmp(cmd, "seed")) {
        // BR13-2 provisioning: set the per-student seed -> synthetic MAC -> per-student
        // service password. In a fielded kit this is injected out-of-band at provisioning
        // and locked; exposed here for bench setup and the grader's known-answer check.
        if (a1) { plc_auth_set_seed(a1); Serial.printf("[SEED] set. device MAC now %s ; service pw regenerated\n", plc_device_mac()); }
        else Serial.printf("[SEED] current: %s\n", *plc_auth_seed() ? plc_auth_seed() : "(none, factory MAC)");
    }
    else if (!strcmp(cmd, "svcinfo")) cmd_svcinfo();
    else if (!plc_diag_console(cmd, a1, a2)) Serial.println("? type 'help'");
}

static void print_banner() {
    Serial.println(F("\n[console] live PLC debug terminal ready. Type 'help'."));
    Serial.println(F("[console] USB-C CDC 115200 8N1  |  a line = command + Enter (LF or CR-LF)"));
    Serial.println(F("[console] logs status/read N retrieves retained diagnostics; 'console' for details"));
    Serial.print(F("plc> "));
}

void plc_console_begin() {
    print_banner();
}

// Non-blocking line reader. Resilient by design:
//  - accepts LF, CR, or CR-LF as end-of-line (any terminal/tool convention)
//  - echoes printable input and supports backspace/DEL so a human can edit
//  - ignores stray control bytes instead of storing them
//  - reports (not silently drops) an over-long line and resyncs the prompt
//  - every received byte defers the telemetry firehose (console_telemetry_muted)
void plc_console_poll() {
    console_health_poll();
    if (modbus_serial_active()) { modbus_serial_poll(); return; }
    if (acquisition_record_busy() || diagnostic_log_console_busy() || runtime_busy() || display_capture_console_busy()) {
        acquisition_record_poll();
        diagnostic_log_console_poll();
        // Consume only the line-feed paired with the preceding CR command.
        if((runtime_busy() || display_capture_console_busy()) && s_last_eol=='\r' && Serial.available() && Serial.peek()=='\n') {
            Serial.read();console_health_rx();s_last_eol=0;
        }
        runtime_poll();
        display_capture_console_poll();
        if (!acquisition_record_busy() && !diagnostic_log_console_busy() && !runtime_busy() && !display_capture_console_busy() && !runtime_failed() && !display_capture_console_failed()) Serial.print(F("plc> "));
        return;
    }
    static bool discard = false;
    unsigned budget = 512;
    while (budget-- && Serial.available() > 0) {
        int c = Serial.read();
        if(c>=0)console_health_rx();
        s_last_input_ms = millis();
        if (c == '\r' || c == '\n') {
            if (c == '\n' && s_last_eol == '\r') { s_last_eol = 0; continue; }  // swallow LF of CR-LF
            s_last_eol = (char)c;
            Serial.println();
            s_line[s_len] = 0;
            if (!discard && s_len > 0) {console_health_command();process(s_line);}
            discard = false;
            s_len = 0;
            if (acquisition_record_busy() || diagnostic_log_console_busy() || runtime_busy() || display_capture_console_busy()) return;
            Serial.print(F("plc> "));
            continue;
        }
        s_last_eol = 0;
        if (discard) continue;
        if (c == 0x08 || c == 0x7F) {                 // backspace / DEL
            if (s_len > 0) { s_len--; Serial.print(F("\b \b")); }
            continue;
        }
        if (c < 0x20) continue;                        // drop other control bytes
        if (s_len < (int)sizeof(s_line) - 1) {
            s_line[s_len++] = (char)c;
            // Echo human commands; an IR frame does not need kilobytes of USB
            // echo competing with its receive/acknowledgement stream.
            if (s_len <= 160 && (s_len < 8 || strncmp(s_line, "PLCLOAD ", 8)) &&
                Serial.availableForWrite() > 0) Serial.write((uint8_t)c);
        } else {
            s_len = 0; discard = true;
            Serial.print(F("\r\n! line too long - discarded through end of line\r\n"));
        }
    }
}
