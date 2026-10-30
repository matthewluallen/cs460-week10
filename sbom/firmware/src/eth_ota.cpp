#include "network_access.h"
// eth_ota.cpp - W5500-native firmware OTA upload on port 80, served over the
// chip's own TCP stack so the QEMU twin and real Ethernet builds expose the
// update path. ota_handler.cpp binds the Arduino WiFi WebServer (WiFi/lwIP) and
// therefore never serves on the W5500-only build; this is the same deliberately
// vulnerable accept over the transport the device actually uses:
//   CWE-494  no signature / hash verification - any binary is accepted
//   CWE-306  no authentication on the upload endpoint
//   CWE-319  cleartext HTTP (no TLS)
//   CWE-022  upload logged to a path built from attacker-influenced input
// The weakness is in the logic, not the transport. Guarded by ENABLE_ETHERNET.
#include "eth_ota.h"
#ifdef ENABLE_ETHERNET
#include "net_types.h"
#include "eth_net.h"
#include "config.h"
#include "diagnostic_log.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <LittleFS.h>
#include <Update.h>

#ifndef ENABLE_CWE_DEMOS
#include "serial_config.h"          // g_cfg credentials (Basic auth)
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include <strings.h>                // strcasecmp
// Device firmware-signing key (provisioning secret). A production device verifies a
// vendor SIGNATURE against a burned-in public key; this HMAC-SHA256 over the image is
// the symmetric teaching stand-in that closes CWE-494 - an attacker without the key
// cannot forge an acceptable image.
static const char* OTA_HMAC_KEY = "stamplc-fw-signing-key-v2";
static bool ota_header(const char* req, const char* name, char* out, size_t outlen) {
    const char* p = strstr(req, name); if (!p) return false;
    p += strlen(name); while (*p == ' ') p++;
    size_t i = 0; while (*p && *p != '\r' && *p != '\n' && i < outlen - 1) out[i++] = *p++;
    out[i] = 0; return true;
}
static bool ota_authed(const char* req) {
    char hv[96]; if (!ota_header(req, "Authorization:", hv, sizeof(hv))) return false;
    const char* b = strstr(hv, "Basic "); if (!b) return false; b += 6;
    unsigned char dec[80]; size_t dl = 0;
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &dl, (const unsigned char*)b, strlen(b)) != 0) return false;
    dec[dl] = 0;
    char expect[52]; snprintf(expect, sizeof(expect), "%s:%s", g_cfg.cfg_user, g_cfg.cfg_pass);
    return strcmp((char*)dec, expect) == 0;
}
static void ota_hex(const unsigned char* d, size_t n, char* out) {
    static const char* hx = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[i*2] = hx[d[i] >> 4]; out[i*2+1] = hx[d[i] & 0xf]; }
    out[n*2] = 0;
}
#endif

static NetServer ota_httpd(OTA_HTTP_PORT);   // :80, the W5500's own TCP stack

void eth_ota_begin() {
    LittleFS.begin(true);
    LittleFS.mkdir(LOG_DIR);
    ota_httpd.begin();
#ifdef ENABLE_CWE_DEMOS
    Serial.printf("[OTA] W5500 OTA server on port %d  (UNSIGNED - CWE-494, no auth - CWE-306)\n", OTA_HTTP_PORT);
#else
    Serial.printf("[OTA] W5500 OTA server on port %d  (signed + authenticated: HMAC-SHA256 + Basic auth)\n", OTA_HTTP_PORT);
#endif
}

void eth_ota_topup() {
    if (eth_listen_count(OTA_HTTP_PORT) == 0) ota_httpd.begin();
}

// CWE-494: the "verification" that always passes. Named so the binary/decompile
// view shows a verify step that is a no-op (return true, unconditionally).
static bool verify_firmware(const uint8_t* /*data*/, size_t /*len*/) {
    return true;   // accept any image - no signature, no hash, no public key
}

// CWE-22: the log path incorporates the source IP with no sanitisation.
static void log_upload(const char* source_ip, size_t size) {
    char log_file[160];
    snprintf(log_file, sizeof(log_file), "%s/ota_%s_%lu.txt",
             LOG_DIR, source_ip, (unsigned long)millis());
    File f = LittleFS.open(log_file, "w");
    if (f) {
        f.printf("Source: %s\nSize: %lu bytes\nVerified: NO (CWE-494)\n",
                 source_ip, (unsigned long)size);
        f.close();
        diagnostic_log_printf(diagnostics::Origin::Application,
                              "[OTA] unsigned image accepted from %s (%lu bytes) -> %s\n",
                              source_ip, (unsigned long)size, log_file);
    }
}

static long content_length(const char* hdr) {
    const char* p = strstr(hdr, "Content-Length:");
    if (!p) p = strstr(hdr, "content-length:");
    if (!p) return -1;
    p += 15;
    while (*p == ' ') p++;
    return atol(p);
}

static void send_simple(NetClient& c, const char* status, const char* ctype, const char* body) {
    String h = "HTTP/1.1 "; h += status;
    h += "\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: "; h += ctype;
    h += "\r\nContent-Length: "; h += (int)strlen(body);
    h += "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n"; h += body;
    c.print(h);
}

void eth_ota_handle() {
    NetClient c = ota_httpd.available();
    if (!c) return;
    if (plc_project_host_acl_enabled()) {
        send_simple(c, "403 Forbidden", "application/json", PLC_ACL_ERROR); c.stop(); return;
    }
    if (!c.connected()) { c.stop(); return; }
    if (!c.available()) return;

    // request line + headers, stop at the blank line
    char req[700]; size_t n = 0; bool done = false;
    unsigned long t0 = millis();
    while (n < sizeof(req) - 1 && millis() - t0 < 500) {
        while (c.available() && n < sizeof(req) - 1) {
            req[n++] = (char)c.read();
            if (n >= 4 && req[n-1]=='\n' && req[n-2]=='\r' && req[n-3]=='\n' && req[n-4]=='\r') { done = true; break; }
        }
        if (done || !c.connected()) break;
    }
    req[n] = 0;

    char method[8] = {0}, target[256] = {0};
    sscanf(req, "%7s %255s", method, target);
    String ips = c.remoteIP().toString();
    const uint32_t peer = plc_network_ipv4(c.remoteIP());

    if (plc_project_host_acl_enabled()) {
        send_simple(c, "403 Forbidden", "application/json", PLC_ACL_ERROR); c.stop(); return;
    }
    if (!strcmp(method, "POST") && !strcmp(target, OTA_ENDPOINT)) {
#ifdef ENABLE_CWE_DEMOS
        long clen = content_length(req);
        if (clen <= 0) clen = OTA_MAX_SIZE;

        // CWE-306: no authentication is checked here at all.
        // CWE-494: no verification gate - verify_firmware() below is a no-op.
        bool begun = false;
        uint32_t admitted_generation = 0;
        {
            PlcNetworkImageGuard guard;
            if (plc_project_host_acl_enabled()) {
                send_simple(c, "403 Forbidden", "application/json", PLC_ACL_ERROR); c.stop(); return;
            }
            admitted_generation = plc_project_generation();
            begun = Update.begin((size_t)clen);
        }
        size_t written = 0;
        uint8_t buf[512];
        unsigned long tb = millis();
        while (written < (size_t)clen && millis() - tb < 8000) {
            while (c.available() && written < (size_t)clen) {
                PlcNetworkImageGuard guard;
                if (!plc_network_deferred_allowed(peer, OTA_ENDPOINT, admitted_generation)) {
                    if (begun) Update.abort();
                    send_simple(c, "403 Forbidden", "application/json", PLC_ACL_ERROR); c.stop(); return;
                }
                int r = c.read(buf, sizeof(buf));
                if (r <= 0) break;
                if (begun) Update.write(buf, (size_t)r);
                written += (size_t)r;
                tb = millis();
            }
            if (!c.connected() && !c.available()) break;
        }

        // The accept has already happened: bytes were taken and written with no
        // signature or auth. That is the CWE-494/306 outcome, logged here.
        (void)verify_firmware(buf, written);     // no-op "check"
        log_upload(ips.c_str(), written);

        {
            // Boot-partition selection and restart are one commit boundary.
            // Releasing the image lock after Update.end would let a new project
            // inherit an already-selected image or an old request's reboot.
            PlcNetworkImageGuard guard;
            if (!plc_network_deferred_allowed(peer, OTA_ENDPOINT, admitted_generation)) {
                if (begun) Update.abort();
                send_simple(c, "403 Forbidden", "application/json", PLC_ACL_ERROR); c.stop(); return;
            }
            const bool boot = begun && Update.end(true); // loader format check, NOT a signature check
            String body = "OTA endpoint accepted an UNSIGNED, UNAUTHENTICATED image (CWE-494/306/319).\n";
            body += "bytes="; body += (int)written; body += " verified=NO\n";
            if (boot) body += "image is bootable - rebooting into it now.\n";
            else      body += "image not a valid ESP app (loader format check) - note: no signature was ever required.\n";
            send_simple(c, "200 OK", "text/plain", body.c_str());

            if (boot) {
                c.flush(); c.stop();
                diagnostic_log_printf(diagnostics::Origin::Application,
                                      "[OTA] rebooting into unsigned image from %s\n", ips.c_str());
                delay(400);
                ESP.restart();   // never returns - boots the attacker image (CWE-494 outcome)
            }
        }
#else
        // Hardened: authenticate the uploader (CWE-306) AND verify an HMAC-SHA256
        // "signature" over the whole image (CWE-494) before anything is committed.
        if (!ota_authed(req)) {
            send_simple(c, "401 Unauthorized", "text/plain",
                        "OTA requires authentication (CWE-306 fixed).\n");
            c.stop(); return;
        }
        char sighex[80] = {0};
        if (!ota_header(req, "X-Firmware-HMAC:", sighex, sizeof(sighex)) || !sighex[0]) {
            send_simple(c, "403 Forbidden", "text/plain",
                        "OTA requires a firmware signature header X-Firmware-HMAC (CWE-494 fixed).\n");
            c.stop(); return;
        }
        long clen = content_length(req);
        if (clen <= 0) clen = OTA_MAX_SIZE;
        const mbedtls_md_info_t* mdi = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_context_t md; mbedtls_md_init(&md);
        mbedtls_md_setup(&md, mdi, 1);
        mbedtls_md_hmac_starts(&md, (const unsigned char*)OTA_HMAC_KEY, strlen(OTA_HMAC_KEY));
        bool begun = false;
        uint32_t admitted_generation = 0;
        {
            PlcNetworkImageGuard guard;
            if (plc_project_host_acl_enabled()) {
                mbedtls_md_free(&md);
                send_simple(c, "403 Forbidden", "application/json", PLC_ACL_ERROR); c.stop(); return;
            }
            admitted_generation = plc_project_generation();
            begun = Update.begin((size_t)clen);
        }
        size_t written = 0; uint8_t buf[512]; unsigned long tb = millis();
        while (written < (size_t)clen && millis() - tb < 8000) {
            while (c.available() && written < (size_t)clen) {
                PlcNetworkImageGuard guard;
                if (!plc_network_deferred_allowed(peer, OTA_ENDPOINT, admitted_generation)) {
                    if (begun) Update.abort();
                    mbedtls_md_free(&md);
                    send_simple(c, "403 Forbidden", "application/json", PLC_ACL_ERROR); c.stop(); return;
                }
                int r = c.read(buf, sizeof(buf));
                if (r <= 0) break;
                mbedtls_md_hmac_update(&md, buf, (size_t)r);   // hash as we go
                if (begun) Update.write(buf, (size_t)r);       // staged, NOT committed
                written += (size_t)r; tb = millis();
            }
            if (!c.connected() && !c.available()) break;
        }
        unsigned char mac[32]; mbedtls_md_hmac_finish(&md, mac); mbedtls_md_free(&md);
        char machex[65]; ota_hex(mac, 32, machex);
        if (strcasecmp(machex, sighex) != 0) {
            if (begun) Update.abort();                          // never commit an unverified image
            diagnostic_log_printf(diagnostics::Origin::Application,
                                  "[OTA] REJECTED forged/unsigned image from %s (bad HMAC)\n", ips.c_str());
            send_simple(c, "403 Forbidden", "text/plain",
                        "firmware signature INVALID - image rejected, not flashed (CWE-494).\n");
            c.stop(); return;
        }
        {
            // Keep final admission, boot selection and restart on this project.
            PlcNetworkImageGuard guard;
            if (!plc_network_deferred_allowed(peer, OTA_ENDPOINT, admitted_generation)) {
                if (begun) Update.abort();
                send_simple(c, "403 Forbidden", "application/json", PLC_ACL_ERROR); c.stop(); return;
            }
            const bool boot = begun && Update.end(true);
            String body = "OTA image authenticated + signature verified; ";
            body += boot ? "rebooting into it now.\n" : "but loader format check failed.\n";
            send_simple(c, "200 OK", "text/plain", body.c_str());
            if (boot) {
                c.flush(); c.stop();
                diagnostic_log_printf(diagnostics::Origin::Application,
                                      "[OTA] booting SIGNED, authenticated image from %s\n", ips.c_str());
                delay(400);
                ESP.restart();
            }
        }
#endif
    } else if (!strcmp(method, "GET") && (!strcmp(target, "/") || !strcmp(target, "/ota"))) {
        send_simple(c, "200 OK", "text/plain",
            "M5StamPLC OTA (teaching). POST a .bin to " OTA_ENDPOINT
            " - no auth, no signature (CWE-494/306/319).\n");
    } else {
        send_simple(c, "404 Not Found", "application/json", "{\"err\":\"nf\"}");
    }
    delay(1);
    c.stop();
}
#else
void eth_ota_begin() {}
void eth_ota_handle() {}
void eth_ota_topup() {}
#endif
