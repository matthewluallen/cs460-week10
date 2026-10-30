// time_sync.cpp - (insecure) SNTP network time client. See time_sync.h.
// All W5500 socket I/O is confined to time_sync_service() (comms core / core 0).
#include "time_sync.h"
#include "plc_core.h"          // plc_log / WriteSource
#include <Arduino.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const uint32_t NTP_UNIX_DELTA = 2208988800UL;   // 1900-01-01 -> 1970-01-01

// ---- stored wall-clock ----
// THE INTENTIONAL WEAKNESS (teaching build): kept in a *signed 32-bit* field. A
// correct controller uses 64-bit time_t; this truncates the network-supplied time
// (CWE-197), so any value past 2038-01-19 03:14:07 UTC wraps negative and the clock
// reads 1902 (1902-04-27). The secure build (#else) stores the full 64-bit epoch and never wraps.
#ifdef ENABLE_CWE_DEMOS
static int32_t  s_epoch32     = 0;
#else
static int64_t  s_epoch64     = 0;
#endif
static uint32_t s_sync_ms     = 0;
static bool     s_valid       = false;
// current / auto-sync source
static char     s_server[16]  = {0};
static uint16_t s_port        = 123;
static uint32_t s_interval_ms = 0;        // 0 = auto-sync disabled
static uint32_t s_last_auto   = 0;

// ---- cross-core mailbox: request (core 1 -> core 0) + result (core 0 -> core 1) ----
static SemaphoreHandle_t s_lock = nullptr;
static bool     s_req_pending = false;
static char     s_req_server[16] = {0};
static uint16_t s_req_port       = 123;
static uint32_t s_result_seq = 0;         // bumped each completed query
static int32_t  s_r32 = 0;
static int64_t  s_r64 = 0;
static bool     s_r_ok = false;
static bool     s_r_tcp = false;

static inline void LOCK()   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static inline void UNLOCK() { if (s_lock) xSemaphoreGive(s_lock); }

void time_sync_format(int64_t epoch, char* buf, int buflen) {
    time_t t = (time_t)epoch;
    struct tm tmv; gmtime_r(&t, &tmv);
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S UTC", &tmv);
}

bool        time_sync_valid()        { return s_valid; }
const char* time_sync_server()       { return s_server; }
uint16_t    time_sync_port()         { return s_port; }
uint32_t    time_sync_interval_ms()  { return s_interval_ms; }

int64_t time_sync_now() {
    if (!s_valid) return 0;
    int64_t drift = (int64_t)((millis() - s_sync_ms) / 1000);
#ifdef ENABLE_CWE_DEMOS
    // CWE-197: the store is int32, so re-truncate after the drift add -> stays wrapped.
    return (int64_t)(int32_t)((int64_t)s_epoch32 + drift);
#else
    return s_epoch64 + drift;                       // full 64-bit, no Y2038 wrap
#endif
}

void time_sync_set_server(const char* ip, uint16_t port, uint32_t interval_ms) {
    LOCK();
    if (ip) { strncpy(s_server, ip, sizeof(s_server) - 1); s_server[sizeof(s_server) - 1] = 0; }
    if (port) s_port = port;
    s_interval_ms = interval_ms;
    s_last_auto = millis();
    UNLOCK();
}

uint32_t time_sync_request(const char* server, uint16_t port) {
    LOCK();
    strncpy(s_req_server, server, sizeof(s_req_server) - 1); s_req_server[sizeof(s_req_server) - 1] = 0;
    s_req_port = port ? port : 123;
    // reflect the source the user just targeted (so `clk` shows it, not the stale default)
    strncpy(s_server, s_req_server, sizeof(s_server) - 1); s_server[sizeof(s_server) - 1] = 0;
    s_port = s_req_port;
    s_req_pending = true;
    uint32_t seq = s_result_seq;
    UNLOCK();
    return seq;
}

bool time_sync_result(uint32_t since_seq, int32_t* o32, int64_t* o64, bool* ok, bool* tcp) {
    bool have = false;
    LOCK();
    if (s_result_seq != since_seq) {
        if (o32) *o32 = s_r32; if (o64) *o64 = s_r64; if (ok) *ok = s_r_ok; if (tcp) *tcp = s_r_tcp;
        have = true;
    }
    UNLOCK();
    return have;
}

void time_sync_apply(int64_t unix_secs) {
    s_sync_ms = millis(); s_valid = true;
#ifdef ENABLE_CWE_DEMOS
    s_epoch32 = (int32_t)unix_secs;                 // CWE-197: 64-bit time -> 32-bit store (wraps)
    struct timeval tv; tv.tv_sec = (time_t)s_epoch32; tv.tv_usec = 0;
#else
    s_epoch64 = unix_secs;                          // hardened: keep the full 64-bit epoch
    struct timeval tv; tv.tv_sec = (time_t)unix_secs; tv.tv_usec = 0;
#endif
    settimeofday(&tv, nullptr);
}

#ifdef ENABLE_ETHERNET
#include <Ethernet_Generic.hpp>
#include "net_types.h"         // NetClient (EthernetClient)
#include "eth_net.h"
#include "serial_config.h"     // g_cfg

static void sntp_request(uint8_t* p) { memset(p, 0, 48); p[0] = 0x23; }   // LI=0 VN=4 Mode=3

// Parse transmit timestamp (bytes 40..43 = seconds since 1900) into the correct
// 64-bit Unix epoch AND the firmware's truncated 32-bit store value. Era-aware:
// NTP's 32-bit seconds field rolls over in 2036, so a post-2036 time arrives small;
// lift it into era 1 to recover the real 64-bit time, then cast to int32_t (the bug).
static bool sntp_parse(const uint8_t* p, int32_t* out32, int64_t* out64) {
    uint32_t secs1900 = ((uint32_t)p[40] << 24) | ((uint32_t)p[41] << 16) |
                        ((uint32_t)p[42] << 8)  |  (uint32_t)p[43];
    if (secs1900 == 0) return false;
    int64_t unix64 = (int64_t)secs1900 - (int64_t)NTP_UNIX_DELTA;
    if (unix64 < 0) unix64 += 4294967296LL;      // era 1 (post-2036)
    if (out64) *out64 = unix64;
    *out32 = (int32_t)unix64;                     // CWE-197: 64-bit time -> 32-bit field
    return true;
}

static bool query_udp(IPAddress ip, uint16_t port, uint8_t* resp) {
    EthernetUDP udp;
    if (!udp.begin(50123)) return false;
    uint8_t req[48]; sntp_request(req);
    udp.beginPacket(ip, port); udp.write(req, sizeof(req)); udp.endPacket();
    uint32_t t0 = millis();
    while (millis() - t0 < 1200) {
        if (udp.parsePacket() >= 48) { udp.read(resp, 48); udp.stop(); return true; }
        delay(10);
    }
    udp.stop();
    return false;
}

static bool query_tcp(IPAddress ip, uint16_t port, uint8_t* resp) {
    NetClient client;
    client.setConnectionTimeout(1200);
    if (!client.connect(ip, port)) { client.stop(); return false; }
    uint8_t req[48]; sntp_request(req);
    client.write(req, sizeof(req));
    uint32_t t0 = millis(); int got = 0;
    while (got < 48 && millis() - t0 < 1200) {
        while (client.available() && got < 48) resp[got++] = (uint8_t)client.read();
        if (got < 48) delay(10);
    }
    client.stop();
    return got >= 48;
}

// UDP first (real SNTP); TCP fallback (traverses the twin's TCP-bridged W5500).
static bool do_query(const char* ip, uint16_t port, int32_t* o32, int64_t* o64, bool* tcp) {
    IPAddress a; if (!a.fromString(ip)) return false;
    uint8_t resp[48]; if (tcp) *tcp = false;
    if (query_udp(a, port, resp)) return sntp_parse(resp, o32, o64);
    if (query_tcp(a, port, resp)) { if (tcp) *tcp = true; return sntp_parse(resp, o32, o64); }
    return false;
}

// CORE 0 ONLY (comms_task). Runs auto-sync on interval, then any posted request.
void time_sync_service() {
    if (s_interval_ms && s_server[0] && (millis() - s_last_auto >= s_interval_ms)) {
        s_last_auto = millis();
        LOCK();
        strncpy(s_req_server, s_server, sizeof(s_req_server) - 1); s_req_server[sizeof(s_req_server) - 1] = 0;
        s_req_port = s_port; s_req_pending = true;
        UNLOCK();
    }
    bool pending = false; char srv[16] = {0}; uint16_t prt = 123;
    LOCK();
    if (s_req_pending) { strncpy(srv, s_req_server, sizeof(srv) - 1); srv[sizeof(srv)-1] = 0; prt = s_req_port; s_req_pending = false; pending = true; }
    UNLOCK();
    if (!pending) return;

    int32_t u32 = 0; int64_t u64 = 0; bool tcp = false;
    bool ok = do_query(srv, prt, &u32, &u64, &tcp);     // socket I/O - core 0 only

    LOCK();
    s_r_ok = ok; s_r32 = u32; s_r64 = u64; s_r_tcp = tcp; s_result_seq++;
    UNLOCK();

    if (ok) {
#ifdef ENABLE_CWE_DEMOS
        time_sync_apply((int64_t)u32);              // truncated store (the bug)
        char when[40]; time_sync_format((int64_t)u32, when, sizeof(when));
        bool truncated = (u64 != (int64_t)u32);
#else
        time_sync_apply(u64);                       // hardened: full 64-bit store
        char when[40]; time_sync_format(u64, when, sizeof(when));
        bool truncated = false;
#endif
        char who[24]; snprintf(who, sizeof(who), "ntp %s", srv);
        plc_log(SRC_SYSTEM, who, "clock <- %s%s", when, truncated ? " (TRUNCATED!)" : "");
    }
}

void time_sync_begin() {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    const char* src = (g_cfg.ntp[0]) ? g_cfg.ntp : g_cfg.eth_gw;
    if (src && src[0]) { strncpy(s_server, src, sizeof(s_server) - 1); s_server[sizeof(s_server) - 1] = 0; }
    s_port = g_cfg.ntp_port ? (uint16_t)g_cfg.ntp_port : 123;
    s_interval_ms = 0; s_last_auto = millis();
}

#else  // no-Ethernet build
void time_sync_service() {}
void time_sync_begin() { if (!s_lock) s_lock = xSemaphoreCreateMutex(); }
#endif
