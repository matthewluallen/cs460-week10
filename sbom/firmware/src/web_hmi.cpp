#include "diagnostic_log.h"
#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include "web_hmi.h"
#include "web_api.h"
#include "process_bindings.h"
#include "source_snapshot.h"
#include "console_health.h"
#include "modbus_tcp_server.h"
#include "plc_core.h"
#include "plc_tags.h"
#include "serial_config.h"
#include "eth_net.h"
#include <LittleFS.h>

WebHMI web_hmi;

WebHMI::WebHMI() : server(WEB_HMI_PORT) {}

bool WebHMI::start() {
    server.on("/", [this]() { handle_root(); });
    server.on("/api/coils", [this]() { handle_coils(); });
    server.on("/api/registers", [this]() { handle_registers(); });
    server.on("/logs", [this]() { handle_logs(); });      // CWE-22: Vulnerable path traversal
    server.on("/api/upload", HTTP_POST, [this]() { handle_api(); });
    server.on("/api/bindings", [this]() { server.send(200,"application/json",plc_bindings_json()); });
    server.on("/api/timing", HTTP_GET, [this]() { server.send(200,"application/json",plc_loop_phases_json()); });
    server.on("/api/scan", HTTP_GET, [this]() { server.send(200,"application/json",plc_scan_json()); });
    server.on("/api/source", HTTP_GET, [this]() { server.send(200,"application/json",plc_source_json()); });
    server.on("/api/console", HTTP_GET, [this]() { server.send(200,"application/json",console_health_json()); });
    server.on("/api/points", [this]() { handle_points(); });
    server.on("/api/set", [this]() { handle_set(); });
    server.on("/api/force", [this]() { handle_force(); });
    server.on("/api/mode", [this]() { handle_mode(); });
    server.on("/api/log", [this]() { handle_hlog(); });
    server.on("/api/config", HTTP_ANY, [this]() { handle_config(); });   // authed-by-default-cred remote config

    server.begin();
    Serial.printf("[Web HMI] Server started on port %d\n", WEB_HMI_PORT);
    return true;
}

void WebHMI::handle() {
    server.handleClient();
}

void WebHMI::handle_root() {
    server.send(200, "text/html", get_html_page());
}

void WebHMI::handle_coils() {
    // Read/write coils via web
    if (server.hasArg("addr") && server.hasArg("state")) {
        uint16_t addr = server.arg("addr").toInt();
        bool state = server.arg("state").toInt() ? true : false;

        modbus_server.set_coil(addr, state);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else if (server.hasArg("addr")) {
        uint16_t addr = server.arg("addr").toInt();
        bool state = modbus_server.get_coil(addr);

        String response = "{\"addr\":";
        response += addr;
        response += ",\"state\":";
        response += state ? "1" : "0";
        response += "}";
        server.send(200, "application/json", response);
    } else {
        server.send(400, "text/plain", "Missing parameters");
    }
}

void WebHMI::handle_registers() {
    // Read/write registers via web
    if (server.hasArg("addr") && server.hasArg("value")) {
        uint16_t addr = server.arg("addr").toInt();
        uint16_t value = server.arg("value").toInt();

        modbus_server.set_register(addr, value);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else if (server.hasArg("addr")) {
        uint16_t addr = server.arg("addr").toInt();
        uint16_t value = modbus_server.get_register(addr);

        String response = "{\"addr\":";
        response += addr;
        response += ",\"value\":";
        response += value;
        response += "}";
        server.send(200, "application/json", response);
    } else {
        server.send(400, "text/plain", "Missing parameters");
    }
}

// CWE-22: Path traversal vulnerability in log viewer
// Student can request "/logs?file=../../config.txt" to read arbitrary files
void WebHMI::handle_logs() {
    if (server.hasArg("file")) {
        String filename = server.arg("file");

        // CWE-22: No path sanitization! Student can inject "../" to traverse directories
        String filepath = String(LOG_DIR) + "/" + filename;

        diagnostic_log_printf(diagnostics::Origin::Application,"[Web] Accessing file: %s\n", filepath.c_str());

        File f = LittleFS.open(filepath, "r");
        if (f) {
            // Read and send file contents
            String content = "";
            while (f.available()) {
                content += (char)f.read();
            }
            f.close();

            server.send(200, "text/plain", content);
        } else {
            server.send(404, "text/plain", "File not found");
        }
    } else {
        // List available logs
        String html = "<h2>Available Logs</h2><ul>";
        File dir = LittleFS.open(LOG_DIR);
        File file = dir.openNextFile();
        while (file) {
            html += "<li><a href=\"/logs?file=";
            html += file.name();
            html += "\">";
            html += file.name();
            html += "</a></li>";
            file = dir.openNextFile();
        }
        html += "</ul>";
        html += "<p><b>Try accessing parent directories:</b> <code>/logs?file=../../config.txt</code></p>";
        server.send(200, "text/html", html);
    }
}

void WebHMI::handle_api() {
    // Generic API endpoint (stub)
    server.send(200, "text/plain", "API OK");
}

void WebHMI::handle_points() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String j = "{\"mode\":\""; j += plc_mode_str();
    j += "\",\"scan\":"; j += plc_scan_last_us();
    j += ",\"scanmax\":"; j += plc_scan_max_us();
    j += ",\"faults\":"; j += plc_fault_active_count();
    j += ",\"forces\":"; j += plc_force_count();
    j += ",\"hb\":"; j += plc_heartbeat();
    j += ",\"map_version\":"; j += pointmap::VERSION;
    j += ",\"map_sha256\":\""; j += pointmap::SHA256;
    j += "\",\"pts\":[";
    for (int i = 0; i < PLC_TAG_COUNT; i++) {
        const TagDef* t = &PLC_TAGS[i];
        int32_t val; bool forced = false, fv = false;
        if (t->kind == TK_COIL) { val = plc_coil_live(t->addr) ? 1 : 0; forced = plc_coil_is_forced(t->addr); fv = plc_coil_forced_val(t->addr); }
        else if (t->kind == TK_DI) { val = plc_di_live((uint8_t)t->addr) ? 1 : 0; forced = plc_di_is_forced((uint8_t)t->addr); fv = plc_di_forced_val((uint8_t)t->addr); }
        else val = plc_tag_decode(t, modbus_server.get_register(t->addr));
        if (i) j += ",";
        j += "{\"n\":\""; j += t->name; j += "\",\"k\":"; j += (int)t->kind;
        j += ",\"v\":"; j += val; j += ",\"u\":\""; j += t->unit;
        j += "\",\"w\":"; j += t->writable ? 1 : 0; j += ",\"c\":"; j += t->forceable ? 1 : 0;
        j += ",\"f\":"; j += forced ? 1 : 0; j += ",\"fv\":"; j += fv ? 1 : 0;
        j += ",\"d\":\""; j += t->desc; j += "\"}";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

void WebHMI::handle_set() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("tag") || !server.hasArg("val")) { server.send(400, "application/json", "{\"err\":\"args\"}"); return; }
    const TagDef* t = plc_tag_find(server.arg("tag").c_str());
    if (!t || !t->writable) { server.send(400, "application/json", "{\"err\":\"not writable\"}"); return; }
    long v = server.arg("val").toInt();
    if (t->kind == TK_COIL) { modbus_server.set_coil(t->addr, v != 0); plc_log(SRC_WEB, "", "SET %s(coil %u) = %d", t->name, t->addr, v ? 1 : 0); }
    else { modbus_server.set_register(t->addr, (uint16_t)v); plc_log(SRC_WEB, "", "SET %s(reg %u) = %ld", t->name, t->addr, v); }
    server.send(200, "application/json", "{\"ok\":1}");
}

// Remote network+NTP config, "protected" by a DEFAULT credential (admin:openplc)
// that ships unchanged (CWE-798/1392). An on-path attacker who knows the documented
// default authenticates and repoints the gateway / NTP server - completing the
// rogue-config -> rogue-time AiTM that steps the clock past 2038. The fix is to
// change the default (`netcfg auth <u> <p>`), and to not expose config off-box.
void WebHMI::handle_config() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.authenticate(g_cfg.cfg_user, g_cfg.cfg_pass)) {
        return server.requestAuthentication(BASIC_AUTH, "StamPLC Config",
            "default admin:openplc (CWE-798/1392) - change it with 'netcfg auth'");
    }
    if (server.method() == HTTP_POST) {
        String ch = "";
        if (server.hasArg("mode")) { g_cfg.use_static = (server.arg("mode") == "static") ? 1 : 0; ch += "mode "; }
        if (server.hasArg("ip"))   { server.arg("ip").toCharArray(g_cfg.ip, sizeof(g_cfg.ip));     ch += "ip "; }
        if (server.hasArg("gw"))   { server.arg("gw").toCharArray(g_cfg.gw, sizeof(g_cfg.gw));     ch += "gw "; }
        if (server.hasArg("mask")) { server.arg("mask").toCharArray(g_cfg.mask, sizeof(g_cfg.mask)); ch += "mask "; }
        if (server.hasArg("ntp"))  { server.arg("ntp").toCharArray(g_cfg.ntp, sizeof(g_cfg.ntp));   ch += "ntp "; }
        if (server.hasArg("ntp_port")) { g_cfg.ntp_port = server.arg("ntp_port").toInt(); ch += "ntp_port "; }
        if (server.hasArg("cfg_user")) { server.arg("cfg_user").toCharArray(g_cfg.cfg_user, sizeof(g_cfg.cfg_user)); ch += "cfg_user "; }
        if (server.hasArg("cfg_pass")) { server.arg("cfg_pass").toCharArray(g_cfg.cfg_pass, sizeof(g_cfg.cfg_pass)); ch += "cfg_pass "; }
        serial_config_save();
        String ip = server.client().remoteIP().toString();
        plc_log(SRC_WEB, ip.c_str(), "REMOTE netcfg via /api/config: %s", ch.c_str());
        server.send(200, "application/json", "{\"saved\":true,\"applied\":\"reboot\",\"changed\":\"" + ch + "\"}");
        return;
    }
    String j = "{\"mode\":\"";
    j += g_cfg.use_static ? "static" : "dhcp";
    j += "\",\"ip\":\"" + String(g_cfg.ip) + "\",\"gw\":\"" + String(g_cfg.gw) + "\",\"mask\":\"" + String(g_cfg.mask) + "\",";
    j += "\"ntp\":\"" + String(g_cfg.ntp[0] ? g_cfg.ntp : g_cfg.gw) + "\",\"ntp_port\":" + String(g_cfg.ntp_port) + ",";
    j += "\"cfg_user\":\"" + String(g_cfg.cfg_user) + "\",";
    j += "\"link\":\"" + (eth_ok() ? eth_ip() : String("no-net")) + "\"}";
    server.send(200, "application/json", j);
}

void WebHMI::handle_force() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("tag")) { server.send(400, "application/json", "{\"err\":\"args\"}"); return; }
    const TagDef* t = plc_tag_find(server.arg("tag").c_str());
    if (!t || !t->forceable) { server.send(400, "application/json", "{\"err\":\"not forceable\"}"); return; }
    bool on = server.arg("on").toInt() != 0;
    bool v = server.arg("val").toInt() != 0;
    if (on) { if (t->kind == TK_COIL) plc_force_coil(t->addr, v, SRC_WEB, ""); else plc_force_di((uint8_t)t->addr, v, SRC_WEB, ""); }
    else    { if (t->kind == TK_COIL) plc_unforce_coil(t->addr, SRC_WEB, ""); else plc_unforce_di((uint8_t)t->addr, SRC_WEB, ""); }
    server.send(200, "application/json", "{\"ok\":1}");
}

void WebHMI::handle_mode() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.hasArg("set")) {
        String m = server.arg("set");
        if (m == "run") plc_set_mode(PLC_RUN, SRC_WEB, "");
        else if (m == "prog") plc_set_mode(PLC_PROGRAM, SRC_WEB, "");
    }
    String j = "{\"mode\":\""; j += plc_mode_str(); j += "\"}";
    server.send(200, "application/json", j);
}

void WebHMI::handle_hlog() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String j = "["; char buf[112]; int n = plc_log_count();
    for (int i = 0; i < n; i++) {
        if (!plc_log_get(i, buf, sizeof(buf))) continue;
        if (j.length() > 1) j += ",";
        j += "\"";
        for (char* p = buf; *p; p++) { if (*p == '"' || *p == '\\') j += '\\'; j += *p; }
        j += "\"";
    }
    j += "]";
    server.send(200, "application/json", j);
}

// CWE-330: Predictable PRNG for session tokens
// Seeded only from millis() - trivial to predict
uint32_t WebHMI::generate_session_token() {
    // CWE-330: Weak random seed
    uint32_t seed = millis() % 10000;  // Only 10,000 possible values!
    return seed ^ 0xDEADBEEF;
}

bool WebHMI::validate_session(const String& token) {
    // CWE-287: No real session validation
    // Just return true for any token
    return true;
}

String WebHMI::get_html_page() { return web_api_html(); }

String web_api_html() {
    // Named-point HMI: view and set the register bits that map to the device's
    // points, like a real PLC/HMI. Data comes from /api/points and /api/log; the
    // "friendly" buttons here and a raw Modbus FC05 write are the SAME action
    // underneath (the CWE-306 lesson). The vulnerable log viewer (CWE-22) and the
    // predictable session token (CWE-330) are kept below on purpose.
    return R"CWEHTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StamPLC HMI</title><style>
:root{--bg:#0e1116;--pan:#171c24;--ln:#2a323d;--tx:#e6edf3;--mut:#8b98a8;--on:#39d353;--off:#3a4351;--amb:#e3b341;--red:#f85149;--blu:#388bfd}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:14px/1.4 system-ui,Segoe UI,Arial}
header{display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:12px 16px;background:var(--pan);border-bottom:1px solid var(--ln)}
h1{font-size:16px;margin:0;letter-spacing:.3px}.badge{padding:3px 10px;border-radius:12px;font-weight:700;font-size:12px}
.run{background:rgba(57,211,83,.15);color:var(--on)}.prog{background:rgba(227,179,65,.15);color:var(--amb)}
.stats{color:var(--mut);font-size:12px;margin-left:auto}
main{padding:16px;display:grid;gap:16px;grid-template-columns:1fr 1fr;max-width:1100px;margin:0 auto}
@media(max-width:820px){main{grid-template-columns:1fr}}
.card{background:var(--pan);border:1px solid var(--ln);border-radius:8px;overflow:hidden}
.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.6px;color:var(--mut);margin:0;padding:10px 14px;border-bottom:1px solid var(--ln)}
table{width:100%;border-collapse:collapse}td{padding:6px 14px;border-bottom:1px solid var(--ln)}
tr:last-child td{border-bottom:0}td:first-child{color:var(--mut)}
tr.forced{background:rgba(227,179,65,.08)}.fb{background:var(--amb);color:#000;font-size:10px;padding:1px 5px;border-radius:8px;font-weight:700}
b.on{color:var(--on)}.off{color:var(--off)}
button{background:#222b36;color:var(--tx);border:1px solid var(--ln);border-radius:5px;padding:3px 8px;margin:1px;cursor:pointer;font-size:12px}
button:hover{border-color:var(--blu)}input{background:#0e1116;color:var(--tx);border:1px solid var(--ln);border-radius:5px;padding:3px}
.log{font:11px/1.5 ui-monospace,Menlo,monospace;color:var(--mut);max-height:230px;overflow:auto;padding:8px 14px}
.warn{grid-column:1/-1;background:rgba(248,81,73,.08);border:1px solid rgba(248,81,73,.3);color:#ffb3ae;border-radius:8px;padding:10px 14px;font-size:12px}
a{color:var(--blu)}
</style></head><body>
<header><h1>StamPLC &middot; PLC HMI</h1>
<span id="mode" class="badge prog">...</span>
<button onclick="mode('run')">RUN</button><button onclick="mode('prog')">PROGRAM</button>
<span id="netsvc" class="badge" title="W5500 socket budget: max 4 concurrent network services">net &ndash;/4</span>
<span id="stats" class="stats"></span></header>
<main>
<div class="card"><h2>Outputs (coils)</h2><table id="coils"></table></div>
<div class="card"><h2>Inputs (discrete)</h2><table id="dis"></table></div>
<div class="card" style="grid-column:1/-1"><h2>Registers</h2><table id="regs"></table></div>
<div class="card" style="grid-column:1/-1"><h2>Network services &middot; <span id="pon">?</span>/<span id="pmax">4</span> enabled (W5500 socket budget)</h2><div id="protos"></div><div style="padding:8px 14px;color:var(--mut);font-size:12px">Changes apply on reboot. <button onclick="reboot()">Reboot to apply</button> <span id="pmsg"></span></div></div>
<div class="card" style="grid-column:1/-1"><h2>Event / audit log (source-attributed)</h2><div id="log" class="log"></div></div>
<div class="warn"><b>Teaching interface.</b> These buttons and a raw unauthenticated Modbus write to the same
address are the identical action (CWE-306). Also live: <a href="/logs?file=../../config.txt">/logs path traversal (CWE-22)</a>,
predictable session token (CWE-330) = <code id="tok"></code>.</div>
</main>
<script>
function esc(s){return String(s).replace(/[<>&]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;'}[c]));}
function row(p){let f=p.f?'forced':'';let v=p.k==2?(p.v+' '+esc(p.u)):(p.v?'<b class=on>1</b>':'<span class=off>0</span>');
let c='';if(p.k==0&&p.w)c+=`<button onclick="setp('${p.n}',1)">ON</button><button onclick="setp('${p.n}',0)">OFF</button>`;
if(p.k==2&&p.w)c+=`<input id="in_${p.n}" size=6 value="${p.v}"><button onclick="setr('${p.n}')">Set</button>`;
if(p.c)c+=`<button onclick="frc('${p.n}',1,1)">F1</button><button onclick="frc('${p.n}',0,1)">F0</button><button onclick="frc('${p.n}',0,0)">clr</button>`;
let b=p.f?` <span class=fb>FORCED ${p.fv}</span>`:'';
return `<tr class="${f}"><td title="${esc(p.d)}">${esc(p.n)}</td><td>${v}${b}</td><td style="text-align:right">${c}</td></tr>`;}
async function tick(){try{
let d=await(await fetch('/api/points')).json();
let m=document.getElementById('mode');m.textContent=d.mode;m.className='badge '+(d.mode=='RUN'?'run':'prog');
let nb=document.getElementById('netsvc');if(d.net!==undefined){nb.textContent='net '+d.net+'/'+d.netmax+(d.net>=d.netmax?' (max)':'');nb.className='badge '+(d.net>=d.netmax?'prog':'run');nb.title='W5500 socket budget: max '+d.netmax+' network services'+(d.net>=d.netmax?' - at limit, disable one to add another':'');}
document.getElementById('stats').textContent=`scan ${d.scan}us (max ${d.scanmax}) · faults ${d.faults} · forces ${d.forces} · hb ${d.hb}`;
let g={0:'',1:'',2:''};d.pts.forEach(p=>g[p.k]+=row(p));
document.getElementById('coils').innerHTML=g[0];document.getElementById('dis').innerHTML=g[1];document.getElementById('regs').innerHTML=g[2];
let lg=await(await fetch('/api/log')).json();document.getElementById('log').innerHTML=lg.map(l=>'<div>'+esc(l)+'</div>').join('');
}catch(e){}}
function setp(n,v){fetch('/api/set?tag='+n+'&val='+v).then(tick);}
function setr(n){let v=document.getElementById('in_'+n).value;fetch('/api/set?tag='+n+'&val='+encodeURIComponent(v)).then(tick);}
function frc(n,v,on){fetch('/api/force?tag='+n+'&val='+v+'&on='+on).then(tick);}
function mode(m){fetch('/api/mode?set='+m).then(tick);}
async function ptick(){try{let d=await(await fetch('/api/proto')).json();
document.getElementById('pon').textContent=d.on;document.getElementById('pmax').textContent=d.max;
let h='<table>';d.svc.filter(s=>s.net).forEach(s=>{h+=`<tr><td title="${s.impl?'':'planned (no port opens)'}">${esc(s.n)}${s.impl?'':' *'}</td><td>:${s.p}</td><td>${s.on?'<b class=on>ON</b>':'<span class=off>off</span>'}</td><td style="text-align:right"><button onclick="proto('${s.n}',${s.on?0:1})">${s.on?'disable':'enable'}</button></td></tr>`;});
document.getElementById('protos').innerHTML=h+'</table>';}catch(e){}}
function proto(n,on){fetch('/api/proto?name='+n+'&on='+on).then(r=>r.json()).then(j=>{
document.getElementById('pmsg').textContent=j.err?('refused: socket budget full ('+j.net+'/'+j.max+') - disable one first'):(esc(j.n)+' '+(j.on?'enabled':'disabled')+' - reboot to apply');ptick();});}
function reboot(){if(confirm('Reboot the PLC now to apply protocol changes? All connections drop for ~20-30s.'))fetch('/api/reboot');}
document.getElementById('tok').textContent=(Date.now()%10000)^0xDEADBEEF;
setInterval(tick,1500);setInterval(ptick,2500);tick();ptick();
</script></body></html>)CWEHTML";
}
