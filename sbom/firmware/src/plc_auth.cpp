#include "plc_auth.h"
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
#ifndef ENABLE_CWE_DEMOS
#include "mbedtls/sha256.h"
#include <string.h>
#endif

// ---- device MAC (the public identifier the service password is derived from) ----
static char  s_mac[18]   = {0};
static uint8_t s_macb[6] = {0};
static char  s_seed[24]  = {0};   // per-student seed (BR13-2): when set, the device
                                  // uses a SYNTHETIC MAC derived from it, so the whole
                                  // credential lab (banner, service pw, map-v2 regs 40-42) is
                                  // per-student and a copied answer is detectably wrong.

static uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static void load_mac() {
    if (s_mac[0]) return;
    if (s_seed[0]) {                              // per-student synthetic MAC
        uint32_t h = fnv1a(s_seed);
        s_macb[0] = 0x02; s_macb[1] = 0x53;       // locally-administered; 0x53 = 'S' (Tbd)
        s_macb[2] = (h >> 24) & 0xff; s_macb[3] = (h >> 16) & 0xff;
        s_macb[4] = (h >> 8) & 0xff;  s_macb[5] = h & 0xff;
    } else {
        esp_efuse_mac_get_default(s_macb);        // factory-unique base MAC (unseeded default)
    }
    snprintf(s_mac, sizeof(s_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             s_macb[0], s_macb[1], s_macb[2], s_macb[3], s_macb[4], s_macb[5]);
}

const char* plc_device_mac() { load_mac(); return s_mac; }
void plc_device_mac_bytes(uint8_t out[6]) { load_mac(); memcpy(out, s_macb, 6); }

// [CWE-798/1392/330] SERVICE password = a fixed transform of the last three MAC
// octets (recoverable). Only used by the teaching login path below; the secure
// build never accepts it.
String plc_service_password() {
    load_mac();
    char buf[16];
    snprintf(buf, sizeof(buf), "SRV-%02X%02X%02X",
             s_macb[3] ^ 0x2A, s_macb[4] ^ 0x2A, s_macb[5] ^ 0x2A);
    return String(buf);
}

// ---- user account ----
#ifdef ENABLE_CWE_DEMOS
static char s_userpw[16] = "1234";   // [CWE-1392/1393] default account operator:1234
#else
// Hardened: no plaintext and no default. A salted SHA-256 hash is stored; empty
// means operator login is disabled until a password is provisioned.
static char s_pwhash[65] = {0};
static const char* PW_SALT = "stamplc-secure-v2:";
static void hash_pw(const char* pw, char out[65]) {
    mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
    mbedtls_sha256_update(&c, (const unsigned char*)PW_SALT, strlen(PW_SALT));
    mbedtls_sha256_update(&c, (const unsigned char*)pw, strlen(pw));
    unsigned char d[32]; mbedtls_sha256_finish(&c, d); mbedtls_sha256_free(&c);
    static const char* hx = "0123456789abcdef";
    for (int i = 0; i < 32; i++) { out[i*2] = hx[d[i] >> 4]; out[i*2+1] = hx[d[i] & 0xf]; }
    out[64] = 0;
}
#endif

void plc_auth_begin() {
    Preferences p;
    p.begin("cfg", true);
    String sd = p.getString("seed", "");          // per-student seed (empty = factory MAC)
#ifdef ENABLE_CWE_DEMOS
    String up = p.getString("userpw", "1234");    // [CWE-256] plaintext at rest
#else
    String hh = p.getString("userpwh", "");       // salted hash only (never the password)
#endif
    p.end();
    sd.toCharArray(s_seed, sizeof(s_seed));
#ifdef ENABLE_CWE_DEMOS
    up.toCharArray(s_userpw, sizeof(s_userpw));
#else
    hh.toCharArray(s_pwhash, sizeof(s_pwhash));
#endif
    load_mac();                                   // now seed-aware
}

const char* plc_auth_seed() { return s_seed; }

void plc_auth_set_seed(const char* seed) {
    strncpy(s_seed, seed ? seed : "", sizeof(s_seed) - 1);
    s_seed[sizeof(s_seed) - 1] = 0;
    Preferences p; p.begin("cfg", false); p.putString("seed", s_seed); p.end();
    s_mac[0] = 0;                                  // invalidate cache -> recompute synthetic MAC
    load_mac();
}

const char* plc_auth_user_password() {
#ifdef ENABLE_CWE_DEMOS
    return s_userpw;                               // [CWE-257] recoverable
#else
    return "(hashed - not recoverable)";
#endif
}

// Password policy.
bool plc_auth_password_allowed(const char* pw, String& why) {
    if (!pw || !*pw) { why = "empty password"; return false; }
    size_t n = strlen(pw);
#ifdef ENABLE_CWE_DEMOS
    // [CWE-521] weak: max 8 chars, A-Z/0-9 only; ACCEPTS weak, REFUSES strong.
    if (n > 8) { why = "too long - max 8 characters (policy)"; return false; }
    for (size_t i = 0; i < n; i++) {
        char c = pw[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) { why = "illegal character - only A-Z and 0-9 allowed (policy)"; return false; }
    }
    why = "accepted (weak policy: <=8 chars, A-Z0-9)";
    return true;
#else
    // Hardened: >= 12 chars and at least 3 of {lower, upper, digit, symbol}.
    if (n < 12) { why = "too short - minimum 12 characters"; return false; }
    bool lo=false, up=false, dg=false, sy=false;
    for (size_t i = 0; i < n; i++) {
        char c = pw[i];
        if (c >= 'a' && c <= 'z') lo = true; else if (c >= 'A' && c <= 'Z') up = true;
        else if (c >= '0' && c <= '9') dg = true; else sy = true;
    }
    if ((int)lo + up + dg + sy < 3) { why = "weak - use >=3 of lower/upper/digit/symbol"; return false; }
    why = "accepted (>=12 chars, mixed character classes)";
    return true;
#endif
}

bool plc_auth_set_user_password(const char* pw, String& msg) {
    if (!plc_auth_password_allowed(pw, msg)) return false;
    Preferences p; p.begin("cfg", false);
#ifdef ENABLE_CWE_DEMOS
    strncpy(s_userpw, pw, sizeof(s_userpw) - 1);
    s_userpw[sizeof(s_userpw) - 1] = 0;
    p.putString("userpw", s_userpw);              // [CWE-256] stored in plaintext
    msg = "password set (stored in plaintext, recoverable)";
#else
    hash_pw(pw, s_pwhash);
    p.putString("userpwh", s_pwhash);             // salted SHA-256 hash only
    msg = "password set (stored as a salted SHA-256 hash)";
#endif
    p.end();
    return true;
}

// ---- session ----
static bool     s_authed = false;
static char     s_who[16] = {0};
static uint32_t s_failed  = 0;
#ifndef ENABLE_CWE_DEMOS
static uint32_t s_lock_until = 0;                 // [CWE-307 fix] lockout window
#endif

bool plc_auth_login(const char* user, const char* pass) {
    if (!user || !pass) { s_failed++; return false; }
#ifndef ENABLE_CWE_DEMOS
    if (s_lock_until && millis() < s_lock_until) return false;   // locked out
#endif
    bool ok = false;
#ifdef ENABLE_CWE_DEMOS
    if (!strcmp(user, "service") && plc_service_password().equals(pass)) ok = true;   // MAC-derived backdoor
    else if (!strcmp(user, "operator") && !strcmp(pass, s_userpw)) ok = true;          // weak user account
    else if (!strcmp(user, "admin") && !strcmp(pass, "openplc")) ok = true;            // legacy hardcoded (CWE-798)
#else
    // No service/MAC backdoor, no hardcoded admin: only a provisioned, hashed operator password.
    if (!strcmp(user, "operator") && s_pwhash[0]) {
        char h[65]; hash_pw(pass, h);
        if (!strcmp(h, s_pwhash)) ok = true;
    }
#endif
    if (ok) {
        s_authed = true; strncpy(s_who, user, sizeof(s_who) - 1); s_who[sizeof(s_who)-1] = 0;
        s_failed = 0;
#ifndef ENABLE_CWE_DEMOS
        s_lock_until = 0;
#endif
    } else {
        s_failed++;
#ifndef ENABLE_CWE_DEMOS
        if (s_failed >= 5) s_lock_until = millis() + 30000;      // 30s lockout after 5 failures
#endif
    }
    return ok;
}

void        plc_auth_logout()       { s_authed = false; s_who[0] = 0; }
bool        plc_authed()            { return s_authed; }
const char* plc_auth_whoami()       { return s_authed ? s_who : "(not logged in)"; }
uint32_t    plc_auth_failed_count() { return s_failed; }

String plc_auth_issue_token() {
#ifdef ENABLE_CWE_DEMOS
    // [CWE-330] predictable token: derived from millis().
    char b[16];
    snprintf(b, sizeof(b), "%08X", (unsigned)(millis() ^ 0xDEADBEEF));
    return String(b);
#else
    // Hardened: 128-bit token from the hardware CSPRNG.
    char b[33];
    for (int i = 0; i < 4; i++) snprintf(b + i*8, 9, "%08X", (unsigned)esp_random());
    return String(b);
#endif
}
