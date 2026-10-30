#include "web_api.h"
#include "plc_core.h"
#include "plc_tags.h"
#include "modbus_tcp_server.h"
#include "plc_auth.h"
#include "eth_net.h"
#include "svc_config.h"
#include "network_access.h"

namespace {struct ProjectImageGuard {ProjectImageGuard(){plc_image_lock();}~ProjectImageGuard(){plc_image_unlock();}};}

String web_api_points_json(uint32_t peer) {
    ProjectImageGuard guard;
    if (!plc_network_http_allowed(peer, "/api/points")) return PLC_ACL_ERROR;
    String j = "{\"mode\":\""; j += plc_mode_str();
    j += "\",\"scan\":"; j += plc_scan_last_us();
    j += ",\"scanmax\":"; j += plc_scan_max_us();
    j += ",\"faults\":"; j += plc_fault_active_count();
    j += ",\"forces\":"; j += plc_force_count();
    j += ",\"hb\":"; j += plc_heartbeat();
    j += ",\"net\":"; j += svc_net_enabled_count();
    j += ",\"netmax\":"; j += SVC_MAX_NET;
    j += ",\"map_version\":"; j += pointmap::VERSION;
    j += ",\"map_sha256\":\""; j += pointmap::SHA256;
    j += "\",\"host_acl_enabled\":"; j += plc_project_host_acl_enabled() ? "true" : "false";
    j += ",\"pts\":[";
    bool emitted = false;
    for (int i = 0; i < PLC_TAG_COUNT; i++) {
        const TagDef* t = &PLC_TAGS[i];
        if (!plc_network_tag_allowed(peer, t, false)) continue;
        const bool may_write = plc_network_tag_allowed(peer, t, true);
        int32_t val; bool forced = false, fv = false;
        if (t->kind == TK_COIL) { val = plc_coil_live(t->addr) ? 1 : 0; forced = plc_coil_is_forced(t->addr); fv = plc_coil_forced_val(t->addr); }
        else if (t->kind == TK_DI) { val = plc_di_live((uint8_t)t->addr) ? 1 : 0; forced = plc_di_is_forced((uint8_t)t->addr); fv = plc_di_forced_val((uint8_t)t->addr); }
        else val = plc_tag_value(t);
        if (emitted) j += ",";
        emitted = true;
        j += "{\"n\":\""; j += plc_json_text(t->name); j += "\",\"k\":"; j += (int)t->kind;
        j += ",\"v\":"; j += val; j += ",\"u\":\""; j += plc_json_text(t->unit);
        j += "\",\"w\":"; j += t->writable && may_write ? 1 : 0; j += ",\"c\":"; j += t->forceable && may_write ? 1 : 0;
        j += ",\"f\":"; j += forced ? 1 : 0; j += ",\"fv\":"; j += fv ? 1 : 0;
        j += ",\"d\":\""; j += plc_json_text(t->desc); j += "\"}";
    }
    j += "]}";
    return j;
}

String web_api_log_json(uint32_t peer) {
    ProjectImageGuard guard;
    if (!plc_network_http_allowed(peer, "/api/log")) return PLC_ACL_ERROR;
    String j = "["; char buf[112]; int n = plc_log_count();
    for (int i = 0; i < n; i++) {
        if (!plc_log_get(i, buf, sizeof(buf))) continue;
        if (j.length() > 1) j += ",";
        j += "\"";
        for (char* p = buf; *p; p++) { if (*p == '"' || *p == '\\') j += '\\'; j += *p; }
        j += "\"";
    }
    j += "]";
    return j;
}

String web_api_mode_json(const char* set, uint32_t peer) {
    ProjectImageGuard guard;
    if (!plc_network_http_allowed(peer, "/api/mode", set && *set)) return PLC_ACL_ERROR;
    if (set && *set) {
        if (!strcmp(set, "run")) plc_set_mode(PLC_RUN, SRC_WEB, "");
        else if (!strcmp(set, "prog")) plc_set_mode(PLC_PROGRAM, SRC_WEB, "");
    }
    String j = "{\"mode\":\""; j += plc_mode_str(); j += "\"}";
    return j;
}

String web_api_set_json(const char* tag, const char* val, uint32_t peer) {
    ProjectImageGuard guard;
    if (!plc_network_http_allowed(peer, "/api/set")) return PLC_ACL_ERROR;
    if (!tag || !*tag || !val) return "{\"err\":\"args\"}";
    const TagDef* t = plc_tag_find(tag);
    if (t && !plc_network_tag_allowed(peer, t, true)) return PLC_ACL_ERROR;
    if (!t || !t->writable) return "{\"err\":\"not writable\"}";
    char* end=nullptr;const long long v=strtoll(val,&end,10);
    if(!val[0]||!end||*end||v<INT32_MIN||v>INT32_MAX)return "{\"err\":\"int32 value required\"}";
    if(!plc_tag_set(t,int32_t(v)))return "{\"err\":\"write refused\"}";
    plc_log(SRC_WEB,"","SET %s = %ld (live value)",t->name,(long)v);
    return "{\"ok\":1}";
}

String web_api_force_json(const char* tag, const char* on, const char* val, uint32_t peer) {
    ProjectImageGuard guard;
    if (!plc_network_http_allowed(peer, "/api/force")) return PLC_ACL_ERROR;
    if (!tag || !*tag) return "{\"err\":\"args\"}";
    const TagDef* t = plc_tag_find(tag);
    if (t && !plc_network_tag_allowed(peer, t, true)) return PLC_ACL_ERROR;
    if (!t || !t->forceable) return "{\"err\":\"not forceable\"}";
    bool bon = on && atoi(on) != 0;
    bool bv  = val && atoi(val) != 0;
    if (bon) { if (t->kind == TK_COIL) plc_force_coil(t->addr, bv, SRC_WEB, ""); else plc_force_di((uint8_t)t->addr, bv, SRC_WEB, ""); }
    else     { if (t->kind == TK_COIL) plc_unforce_coil(t->addr, SRC_WEB, ""); else plc_unforce_di((uint8_t)t->addr, SRC_WEB, ""); }
    return "{\"ok\":1}";
}

String web_api_raw_json(bool coil, const char* address, const char* value, uint32_t peer) {
    ProjectImageGuard guard;
    if (!plc_network_http_allowed(peer, coil ? "/api/coils" : "/api/registers")) return PLC_ACL_ERROR;
    char* end = nullptr;
    const long index = address ? strtol(address, &end, 10) : -1;
    if (!address || !address[0] || !end || *end || index < 0 || index >= (coil ? 512 : 256))
        return "{\"err\":\"invalid address\"}";
    const bool write = value != nullptr;
    if (!plc_project_point_allowed(peer, coil ? point_policy::Kind::Coil : point_policy::Kind::Register, uint16_t(index), write))
        return PLC_ACL_ERROR;
    long v = 0;
    if (write) {
        end = nullptr; v = strtol(value, &end, 10);
        if (!value[0] || !end || *end || v < 0 || v > (coil ? 1 : 65535)) return "{\"err\":\"invalid value\"}";
        if (coil) modbus_server.set_coil(uint16_t(index), v != 0);
        else modbus_server.set_register(uint16_t(index), uint16_t(v));
    } else v = coil ? modbus_server.get_coil(uint16_t(index)) : modbus_server.get_register(uint16_t(index));
    String j = "{\"addr\":"; j += index;
    j += coil ? ",\"state\":" : ",\"value\":"; j += v;
    j += ",\"ok\":1}"; return j;
}


// [CWE-200/1273] IP "banner": hands out the device MAC (the input to the
// service-account password derivation) to any unauthenticated HTTP client.
String web_api_sys_json() {
    String j = "{\"model\":\"M5StamPLC\",\"fw\":\"teaching-v2\",\"mac\":\"";
    j += plc_device_mac();
    j += "\",\"accounts\":[\"operator\",\"service\",\"admin\"]";
    j += ",\"authed\":"; j += plc_authed() ? 1 : 0;
    j += ",\"failed_logins\":"; j += plc_auth_failed_count();
    j += ",\"uptime_s\":"; j += (unsigned long)(millis()/1000);
    j += ",\"listeners\":"; j += eth_listen_count();
    j += ",\"sock_timeout_s\":"; j += (unsigned long)(eth_get_sock_timeout_ms()/1000);
    j += ",\"sock_reaped\":"; j += (unsigned long)eth_reaped_count();
    j += ",\"net_services\":"; j += svc_net_enabled_count();
    j += ",\"net_max\":"; j += SVC_MAX_NET;
    j += "}";
    return j;
}

// GET /api/reboot - soft-reboot the CPU over the network. Intentionally
// unauthenticated, consistent with the rest of this teaching API (CWE-306), and
// the supported remote recovery when the USB console wedges. The caller does
// ESP.restart() after the response is flushed; here we just record intent.
// Logged source=WEB so the reboot shows up in the audit trail.
String web_api_reboot_json() {
    plc_log(SRC_WEB, "", "REBOOT via HTTP /api/reboot");
    return String("{\"ok\":1,\"msg\":\"reboot requested; cancelled if project or policy changes\"}");
}

// GET /api/proto - list services + the socket budget (same data as the serial
// \"proto\" command). One JSON model so the HMI editor and console stay in sync.
String web_api_proto_json() {
    String j = "{\"max\":"; j += SVC_MAX_NET; j += ",\"on\":"; j += svc_net_enabled_count(); j += ",\"svc\":[";
    for (int i = 0; i < SVC_COUNT; i++) { const SvcDef& d = SVC_DEFS[i]; if (i) j += ",";
        j += "{\"n\":\""; j += d.name; j += "\",\"p\":"; j += d.port;
        j += ",\"on\":"; j += svc_enabled(i) ? 1 : 0; j += ",\"net\":"; j += svc_is_net(i) ? 1 : 0;
        j += ",\"impl\":"; j += d.implemented ? 1 : 0; j += "}"; }
    j += "]}"; return j;
}

// GET /api/proto?name=<svc>&on=<0|1> - enable/disable a service. Goes through the
// shared svc_set(), so the 4-service socket budget is enforced here exactly as on
// the console; a refused enable returns err=budget. Change applies on reboot.
String web_api_proto_set_json(const char* name, const char* on) {
    int idx = svc_find(name ? name : "");
    if (idx < 0) return String("{\"err\":\"unknown\"}");
    bool want = on && !strcmp(on, "1");
    bool ok = svc_set(idx, want);
    plc_log(SRC_WEB, "", "proto %s -> %s%s", SVC_DEFS[idx].name, want ? "ON" : "off", ok ? "" : " (REFUSED: budget)");
    String j = "{\"n\":\""; j += SVC_DEFS[idx].name; j += "\",\"ok\":"; j += ok ? 1 : 0;
    j += ",\"on\":"; j += svc_enabled(idx) ? 1 : 0; j += ",\"net\":"; j += svc_net_enabled_count();
    j += ",\"max\":"; j += SVC_MAX_NET; if (!ok) j += ",\"err\":\"budget\"";
    j += ",\"note\":\"reboot to apply\"}"; return j;
}

// [CWE-319] credentials arrive in cleartext; [CWE-307] no lockout;
// [CWE-330] the issued token is predictable (millis-based).
String web_api_login_json(const char* user, const char* pass) {
    if (plc_auth_login(user ? user : "", pass ? pass : "")) {
        String j = "{\"ok\":1,\"who\":\""; j += plc_auth_whoami();
        j += "\",\"token\":\""; j += plc_auth_issue_token(); j += "\"}";
        return j;
    }
    String j = "{\"ok\":0,\"err\":\"bad credentials\",\"attempts\":";
    j += plc_auth_failed_count(); j += "}";
    return j;
}
