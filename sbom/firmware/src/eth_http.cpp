#include "eth_http.h"
#ifdef ENABLE_ETHERNET
#include "net_types.h"
#include "web_api.h"
#include "process_bindings.h"
#include "source_snapshot.h"
#include "console_health.h"
#include "plc_auth.h"
#include "eth_net.h"
#include "config_explain.h"
#include "web_config.h"  // SETUP_PAGE (extern) + config_explain_json_str
#include <WiFi.h>            // WiFi.softAPIP/status for the per-interface config JSON
#include "config.h"
#include "serial_config.h"   // g_cfg + serial_config_save (remote netcfg)
#include "time_sync.h"       // push a changed NTP source live
#include "plc_core.h"        // plc_log
#include "plc_project.h"
#include "network_access.h"
#include "mbedtls/base64.h"  // decode HTTP Basic auth
#include <LittleFS.h>

static NetServer httpd(WEB_HMI_PORT);

void eth_http_begin() {
    LittleFS.begin(true);   // for the /logs CWE-22 teaching endpoint
    httpd.begin();
    Serial.printf("[HTTP] W5500 HTTP server on port %d (HMI + JSON API)\n", WEB_HMI_PORT);
}

// keep at least one HTTP listener while leaving sockets for Modbus + connections
void eth_http_topup() {
    // One waiting HTTP connection is enough for this sequential server. Filling
    // the global pool with HTTP listeners starves active Modbus clients.
    if (eth_listen_count(WEB_HMI_PORT) == 0) httpd.begin();
}

// --- tiny query-arg helper: find key= in the query string, URL-decode value ---
static bool qarg(const char* q, const char* key, char* out, size_t outsz) {
    size_t klen = strlen(key);
    const char* p = q;
    while (p && *p) {
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            const char* v = p + klen + 1;
            size_t o = 0;
            while (*v && *v != '&' && o < outsz - 1) {
                if (*v == '%' && v[1] && v[2]) {
                    auto hx = [](char c)->int{ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return 0; };
                    out[o++] = (char)((hx(v[1]) << 4) | hx(v[2])); v += 3;
                } else if (*v == '+') { out[o++] = ' '; v++; }
                else out[o++] = *v++;
            }
            out[o] = 0;
            return true;
        }
        const char* amp = strchr(p, '&');
        p = amp ? amp + 1 : nullptr;
    }
    out[0] = 0;
    return false;
}

static void send_resp(NetClient& c, int code, const char* ctype, const String& body) {
    String h = "HTTP/1.1 "; h += code; h += (code == 200 ? " OK" : (code == 403 ? " Forbidden" : (code == 404 ? " Not Found" : " Bad Request")));
    h += "\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: "; h += ctype;
    h += "\r\nContent-Length: "; h += body.length();
#ifdef ENABLE_CWE_DEMOS
    // [CWE-200/1273] IP banner: leak the device MAC on every HTTP response.
    h += "\r\nX-Device-MAC: "; h += plc_device_mac();
#endif
    // hardened: no device identifier is leaked in response headers (CWE-200 fixed)
    h += "\r\nServer: M5StamPLC/teaching-v2";
    h += "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
    c.print(h);
    // write the body in chunks (W5500 socket TX buffer is limited)
    const char* p = body.c_str(); size_t left = body.length();
    while (left) { size_t n = left > 1024 ? 1024 : left; c.write((const uint8_t*)p, n); p += n; left -= n; }
}

// CWE-22 teaching endpoint preserved on the Ethernet path too: /logs?file=...
static String read_logfile(const char* fname) {
#ifdef ENABLE_CWE_DEMOS
    String path = "/"; path += fname;           // no sanitisation on purpose (CWE-22)
#else
    // Hardened (CWE-22 fixed): accept only a bare filename - reject any path separator
    // or ".." so the viewer cannot escape to arbitrary files.
    for (const char* p = fname; *p; p++)
        if (*p == '/' || *p == '\\') return String("rejected: illegal path (CWE-22 blocked)");
    if (strstr(fname, "..")) return String("rejected: path traversal blocked (CWE-22)");
    String path = "/"; path += fname;
#endif
    File f = LittleFS.open(path, "r");
    if (!f) return String("File not found: ") + path;
    String out; while (f.available() && out.length() < 4096) out += (char)f.read();
    f.close();
    return out;
}

// --- /api/config: remote network+NTP config, "protected" by a DEFAULT credential ---
// Ships admin:openplc unchanged (CWE-798/1392). An on-path attacker who knows the
// documented default authenticates and repoints the gateway/NTP source, completing
// the rogue-config -> rogue-time AiTM. Fix = change the default (netcfg auth) and
// do not expose config off-box.
static bool config_authed(const char* req) {
    const char* h = strstr(req, "Authorization: Basic ");
    if (!h) return false;
    h += 21;
    char b64[88] = {0}; int i = 0;
    while (*h && *h != '\r' && *h != '\n' && i < (int)sizeof(b64) - 1) b64[i++] = *h++;
    unsigned char dec[66]; size_t dlen = 0;
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &dlen, (const unsigned char*)b64, strlen(b64)) != 0) return false;
    dec[dlen] = 0;
    char expect[52]; snprintf(expect, sizeof(expect), "%s:%s", g_cfg.cfg_user, g_cfg.cfg_pass);
    return strcmp((char*)dec, expect) == 0;
}

static String config_json() { return web_config_json(); }

static String config_apply(const char* query, const char* client_ip, bool commit) {
    return web_config_apply([&](const char* k, char* o, size_t n){ return qarg(query, k, o, n); }, client_ip, commit);
}

static int hexnib(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

void eth_http_handle() {
    NetClient c = httpd.available();
    if (!c) return;
    if (!c.connected()) { c.stop(); return; }
    if (!c.available()) return;

    // read request line + headers (stop at blank line, cap size, short timeout)
    char req[700]; size_t n = 0; bool done = false;
    unsigned long t0 = millis();
    while (n < sizeof(req) - 1 && millis() - t0 < 250) {
        while (c.available() && n < sizeof(req) - 1) {
            req[n++] = (char)c.read();
            if (n >= 4 && req[n-1]=='\n' && req[n-2]=='\r' && req[n-3]=='\n' && req[n-4]=='\r') { done = true; break; }
        }
        if (done || !c.connected()) break;
    }
    req[n] = 0;

    char method[8] = {0}, target[512] = {0};
    sscanf(req, "%7s %511s", method, target);
    char* q = strchr(target, '?'); char query[400] = {0};
    if (q) { *q = 0; strncpy(query, q + 1, sizeof(query) - 1); }

    const uint32_t peer = plc_network_ipv4(c.remoteIP());
    char a1[96] = {}, a2[96] = {}, a3[16] = {};
    int code = 200; const char* type = "application/json"; String body;
    bool challenge = false, reboot = false, config_dirty = false;
    uint32_t admitted_generation = 0;
    auto respond = [&](int status, const char* content_type, const String& value) {
        code = status; type = content_type; body = value;
        if (body == PLC_ACL_ERROR) code = 403;
    };
    // Keep admission, any point access and response construction on one project
    // generation. Socket writes happen after releasing the image lock.
    {
        PlcNetworkImageGuard guard;
        admitted_generation = plc_project_generation();
        const bool mutation = (!strcmp(target, "/api/mode") && qarg(query, "set", a1, sizeof(a1)) && a1[0]) ||
            (!strcmp(target, "/api/proto") && qarg(query, "name", a1, sizeof(a1)) && a1[0]) ||
            (!strcmp(target, "/api/config") && !strcmp(method, "POST"));
        if (!plc_network_http_allowed(peer, target, mutation)) respond(403, "application/json", PLC_ACL_ERROR);
        else {
            if (!strcmp(target, "/") || !strcmp(target, "/index.html")) {
                respond(200, "text/html; charset=utf-8", web_api_html());
            } else if (!strcmp(target, "/api/console")) {
                respond(200, "application/json", console_health_json());
            } else if (!strcmp(target, "/api/timing")) {
                respond(200, "application/json", plc_loop_phases_json());
            } else if (!strcmp(target, "/api/scan")) {
                respond(200, "application/json", plc_scan_json());
            } else if (!strcmp(target, "/api/execution")) {
                respond(200, "application/json", plc_execution_json());
            } else if (!strcmp(target, "/api/source")) {
                respond(200, "application/json", plc_source_json());
            } else if (!strcmp(target, "/api/bindings")) {
                respond(200, "application/json", plc_bindings_json());
            } else if (!strcmp(target, "/api/project")) {
                respond(200, "application/json", plc_project_json());
            } else if (!strcmp(target, "/api/points")) {
                respond(200, "application/json", web_api_points_json(peer));
            } else if (!strcmp(target, "/setup")) {
                respond(200, "text/html; charset=utf-8", SETUP_PAGE);
            } else if (!strcmp(target, "/api/explain")) {
                respond(200, "application/json", config_explain_json_str());
            } else if (!strcmp(target, "/api/log")) {
                respond(200, "application/json", web_api_log_json(peer));
            } else if (!strcmp(target, "/api/mode")) {
                qarg(query, "set", a1, sizeof(a1));
                respond(200, "application/json", web_api_mode_json(a1, peer));
            } else if (!strcmp(target, "/api/set")) {
                qarg(query, "tag", a1, sizeof(a1)); qarg(query, "val", a2, sizeof(a2));
                respond(200, "application/json", web_api_set_json(a1, a2, peer));
            } else if (!strcmp(target, "/api/force")) {
                qarg(query, "tag", a1, sizeof(a1)); qarg(query, "on", a3, sizeof(a3)); qarg(query, "val", a2, sizeof(a2));
                respond(200, "application/json", web_api_force_json(a1, a3, a2, peer));
            } else if (!strcmp(target, "/api/tag")) {
                // [CWE-787] unauthenticated "set tag label": the attacker controls both the bytes
                // and the length (hex in ?data=), copied into a fixed buffer with no bound. In the
                // teaching build an overlong value overflows into the I/O force image and actuates a
                // relay; the secure build bounds-checks the copy (see plc_tag_write).
                char hx[256]; qarg(query, "data", hx, sizeof(hx));
                uint8_t buf[128]; size_t nb = 0;
                for (size_t i = 0; hx[i] && hx[i+1] && nb < sizeof(buf); i += 2) {
                    int hi = hexnib(hx[i]), lo = hexnib(hx[i+1]);
                    if (hi < 0 || lo < 0) break;
                    buf[nb++] = (uint8_t)((hi << 4) | lo);
                }
                plc_tag_write(buf, nb);
                respond(200, "application/json", String("{\"tag_bytes\":") + (int)nb + "}");
            } else if (!strcmp(target, "/api/sys")) {
                respond(200, "application/json", web_api_sys_json());
            } else if (!strcmp(target, "/api/proto")) {
                qarg(query, "name", a1, sizeof(a1));
                if (a1[0]) { qarg(query, "on", a3, sizeof(a3)); respond(200, "application/json", web_api_proto_set_json(a1, a3)); }
                else respond(200, "application/json", web_api_proto_json());
            } else if (!strcmp(target, "/api/login")) {
                qarg(query, "user", a1, sizeof(a1)); qarg(query, "pass", a2, sizeof(a2));
                respond(200, "application/json", web_api_login_json(a1, a2));
            } else if (!strcmp(target, "/api/config")) {
                if (!config_authed(req)) {
                    challenge = true;
                } else if (!strcmp(method, "POST")) {
                    String cip = c.remoteIP().toString();
                    String ch = config_apply(query, cip.c_str(), /*commit=*/false);   // D1: defer the flash write to outside the image lock
                    config_dirty = true;
                    respond(200, "application/json", String("{\"saved\":true,\"applied\":\"reboot\",\"changed\":\"") + ch + "\"}");
                } else {
                    respond(200, "application/json", config_json());
                }
            } else if (!strcmp(target, "/api/reboot")) {
                respond(200, "application/json", web_api_reboot_json());
                reboot = true;
            } else if (!strcmp(target, "/logs")) {
                qarg(query, "file", a1, sizeof(a1));
                respond(200, "text/plain", read_logfile(a1[0] ? a1 : "config.txt"));
            } else {
                respond(404, "application/json", "{\"err\":\"nf\"}");
            }
        }
    }
    if (challenge) {
        c.print(F("HTTP/1.1 401 Unauthorized\r\n"
                  "WWW-Authenticate: Basic realm=\"StamPLC Config - default admin:openplc (CWE-798/1392), change it\"\r\n"
                  "Access-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    } else send_resp(c, code, type, body);
    if (config_dirty) web_config_commit();   // D1: NVS write + NTP apply now run with the image lock released
    if (reboot) {
        c.flush(); c.stop(); delay(400);
        PlcNetworkImageGuard guard;
        if (plc_network_deferred_allowed(peer, "/api/reboot", admitted_generation)) ESP.restart();
        else plc_log(SRC_WEB, "", "REBOOT cancelled: project or network policy changed after admission");
    }
    delay(1);
    c.stop();
}
#else
void eth_http_begin() {}
void eth_http_handle() {}
void eth_http_topup() {}
#endif
