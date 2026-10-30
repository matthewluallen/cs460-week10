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
#include "plc_project.h"
#include "network_access.h"
#include "serial_config.h"
#include "eth_net.h"
#include "config_explain.h"
#include "web_config.h"
#include <WiFi.h>
#include <LittleFS.h>

namespace {
void send_json(WebServer& server, const String& body) {
    server.send(body == PLC_ACL_ERROR ? 403 : 200, "application/json", body);
}
template<class Build> void send_checked(WebServer& server, const char* path, Build build) {
    send_json(server, plc_network_json(plc_network_ipv4(server.client().remoteIP()), path, build));
}
}

// On-device provisioning page (served at /setup over the setup-AP at
// http://192.168.4.1:8080/setup). Educational: each field's ⓘ pulls teaching from the
// device's own /api/explain (the same table the console 'explain' command uses).
const char SETUP_PAGE[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>StampLC Setup</title><style>
body{font:15px system-ui,sans-serif;margin:0;background:#0f1419;color:#e6edf3}.wrap{max-width:600px;margin:0 auto;padding:16px}
h1{font-size:19px}fieldset{border:1px solid #2a3340;border-radius:10px;margin:14px 0;padding:12px}legend{color:#7ee787;padding:0 6px}
.lbl{display:flex;align-items:center;gap:6px;margin:10px 0 2px;color:#9da7b3}
.ig{cursor:pointer;color:#a371f7;font-weight:700;font-size:10px;border:1px solid #2a3340;border-radius:50%;width:16px;height:16px;display:inline-flex;align-items:center;justify-content:center}
.tc{display:none;margin:3px 0 6px;padding:8px 10px;border-left:2px solid #a371f7;background:#161b22;border-radius:0 6px 6px 0;font-size:12.5px;line-height:1.5;color:#9da7b3}
.tc.show{display:block}.tc b{color:#e6edf3}.tc .s{color:#e3b341}
input,select{width:100%;box-sizing:border-box;padding:9px;border-radius:7px;border:1px solid #2a3340;background:#161b22;color:#e6edf3}
button{margin-top:14px;width:100%;padding:12px;border:0;border-radius:8px;background:#238636;color:#fff;font-size:16px}
.learn{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:#9da7b3;margin-left:12px}.muted{color:#6b7684;font-size:13px}.ok{color:#7ee787}
.note{margin:8px 0;padding:8px 10px;border-radius:6px;background:#161b22;border:1px solid #2a3340;font-size:12.5px;color:#9da7b3;display:none}
</style></head><body><div class=wrap>
<h1>StampLC network setup <label class=learn><input type=checkbox id=learn> Learn mode</label></h1>
<p class=muted>Configure each interface, Save, then reboot. Tap the i on any setting to learn what it is and what the firmware does.</p>
<div id=note class=note></div>
<fieldset><legend>WiFi (always present)</legend>
<div class=lbl>Mode <i class=ig data-ck=wifi_mode>i</i></div><div class=tc data-tk=wifi_mode></div>
<select id=wifi_mode onchange=tgl()><option value=off>Off (contained)</option><option value=sta>STA - join a network</option><option value=ap>AP - setup/provisioning</option></select>
<div id=sta_box>
<div class=lbl>SSID <i class=ig data-ck=ssid>i</i></div><div class=tc data-tk=ssid></div><input id=ssid>
<div class=lbl>Password <i class=ig data-ck=pass>i</i></div><div class=tc data-tk=pass></div><input id=pass type=password>
<div class=lbl>Addressing <i class=ig data-ck=wifi_addr>i</i></div><div class=tc data-tk=wifi_addr></div>
<select id=wifi_addr onchange=tgl()><option value=dhcp>DHCP</option><option value=static>Static</option></select>
<div id=wsta_box>
<div class=lbl>IP <i class=ig data-ck=wifi_ip>i</i></div><div class=tc data-tk=wifi_ip></div><input id=wifi_ip>
<div class=lbl>Gateway <i class=ig data-ck=wifi_gw>i</i></div><div class=tc data-tk=wifi_gw></div><input id=wifi_gw>
<div class=lbl>Subnet mask <i class=ig data-ck=wifi_mask>i</i></div><div class=tc data-tk=wifi_mask></div><input id=wifi_mask>
</div></div>
<div id=ap_box class=muted>AP broadcasts <b id=apn></b>; connect and reload this page at http://192.168.4.1:8080/setup.</div>
</fieldset>
<fieldset><legend>Ethernet (W5500) <span id=ethp class=muted></span></legend><div id=eth_box>
<div class=lbl>Addressing <i class=ig data-ck=eth_mode>i</i></div><div class=tc data-tk=eth_mode></div>
<select id=eth_mode onchange=tgl()><option value=dhcp>DHCP</option><option value=static>Static</option></select>
<div id=esta_box>
<div class=lbl>IP <i class=ig data-ck=eth_ip>i</i></div><div class=tc data-tk=eth_ip></div><input id=eth_ip>
<div class=lbl>Gateway <i class=ig data-ck=eth_gw>i</i></div><div class=tc data-tk=eth_gw></div><input id=eth_gw>
<div class=lbl>Subnet mask <i class=ig data-ck=eth_mask>i</i></div><div class=tc data-tk=eth_mask></div><input id=eth_mask>
</div></div></fieldset>
<button onclick=save()>Save</button><p id=msg class=muted></p>
</div><script>
function g(i){return document.getElementById(i)}
function tgl(){g('sta_box').style.display=g('wifi_mode').value=='sta'?'':'none';g('ap_box').style.display=g('wifi_mode').value=='ap'?'':'none';g('wsta_box').style.display=(g('wifi_mode').value=='sta'&&g('wifi_addr').value=='static')?'':'none';g('esta_box').style.display=g('eth_mode').value=='static'?'':'none';note()}
function note(){var m=[];if(g('wifi_mode').value=='ap')m.push('⚠ Setup-AP uses a default password (CWE-798/1392) - change it or use AP only briefly.');if(g('eth_mode').value=='dhcp'||(g('wifi_mode').value=='sta'&&g('wifi_addr').value=='dhcp'))m.push('ℹ DHCP trusts whoever answers first - a rogue server can become your gateway and time source.');var n=g('note');if(m.length){n.style.display='';n.innerHTML=m.join('<br>')}else n.style.display='none'}
var EX={};
fetch('/api/explain').then(r=>r.json()).then(d=>{(d.items||[]).forEach(function(it){EX[it.k]=it});paint()}).catch(function(){});
function card(k){var e=EX[k];if(!e)return 'No description.';var h='<b>'+e.what+'</b>';if(e.fw)h+='<br><b>In the firmware:</b> '+e.fw;if(e.sec)h+='<br><span class=s><b>Security:</b> '+e.sec+'</span>';if(e.cmd)h+='<br><b>Console:</b> '+e.cmd;return h}
function paint(){document.querySelectorAll('.tc').forEach(function(d){d.innerHTML=card(d.getAttribute('data-tk'))})}
document.querySelectorAll('.ig').forEach(function(b){b.onclick=function(){var t=document.querySelector('.tc[data-tk="'+b.getAttribute('data-ck')+'"]');if(t)t.classList.toggle('show')}});
g('learn').onchange=function(){var on=g('learn').checked;document.querySelectorAll('.tc').forEach(function(t){t.classList.toggle('show',on)})};
fetch('/api/config').then(r=>r.json()).then(c=>{g('wifi_mode').value=c.wifi.mode;g('ssid').value=c.wifi.ssid;g('wifi_addr').value=c.wifi.addr;g('wifi_ip').value=c.wifi.ip;g('wifi_gw').value=c.wifi.gw;g('wifi_mask').value=c.wifi.mask;g('apn').textContent=c.wifi.ap_ssid;g('ethp').textContent=c.eth.present?'(present, link '+c.eth.link+')':'(not present)';g('eth_box').style.display=c.eth.present?'':'none';g('eth_mode').value=c.eth.mode;g('eth_ip').value=c.eth.ip;g('eth_gw').value=c.eth.gw;g('eth_mask').value=c.eth.mask;tgl()}).catch(e=>{g('msg').textContent='load failed: '+e});
function save(){var b=new URLSearchParams();['wifi_mode','ssid','pass','wifi_addr','wifi_ip','wifi_gw','wifi_mask','eth_mode','eth_ip','eth_gw','eth_mask'].forEach(function(k){b.append(k,g(k).value)});fetch('/api/config?'+b.toString(),{method:'POST'}).then(r=>r.json()).then(j=>{g('msg').className='ok';g('msg').textContent='Saved ('+j.changed+'). Reboot to apply.'}).catch(e=>{g('msg').textContent='save failed: '+e})}
</script></body></html>)HTML";


WebHMI web_hmi;

WebHMI::WebHMI() : server(WEB_HMI_PORT) {}

bool WebHMI::start() {
    server.on("/", [this]() { handle_root(); });
    server.on("/api/coils", [this]() { handle_coils(); });
    server.on("/api/registers", [this]() { handle_registers(); });
    server.on("/logs", [this]() { handle_logs(); });      // CWE-22: Vulnerable path traversal
    server.on("/api/upload", HTTP_POST, [this]() { handle_api(); });
    server.on("/api/bindings", [this]() { send_checked(server,"/api/bindings",[](){return plc_bindings_json();}); });
    server.on("/api/timing", HTTP_GET, [this]() { send_checked(server,"/api/timing",[](){return plc_loop_phases_json();}); });
    server.on("/api/scan", HTTP_GET, [this]() { send_checked(server,"/api/scan",[](){return plc_scan_json();}); });
    server.on("/api/execution", HTTP_GET, [this]() { send_checked(server,"/api/execution",[](){return plc_execution_json();}); });
    server.on("/api/source", HTTP_GET, [this]() { send_checked(server,"/api/source",[](){return plc_source_json();}); });
    server.on("/api/console", HTTP_GET, [this]() { send_checked(server,"/api/console",[](){return console_health_json();}); });
    server.on("/api/points", [this]() { handle_points(); });
    server.on("/api/project", [this]() { send_checked(server,"/api/project",[](){return plc_project_json();}); });
    server.on("/api/set", [this]() { handle_set(); });
    server.on("/api/force", [this]() { handle_force(); });
    server.on("/api/mode", [this]() { handle_mode(); });
    server.on("/api/log", [this]() { handle_hlog(); });
    server.on("/api/config", HTTP_ANY, [this]() { handle_config(); });   // authed-by-default-cred remote config
    server.on("/setup", [this]() { server.send(200, "text/html", SETUP_PAGE); });  // on-device provisioning UI (reachable over the setup-AP)
    server.on("/api/explain", [this]() { server.sendHeader("Access-Control-Allow-Origin","*"); server.send(200, "application/json", config_explain_json_str()); });  // teaching table for the /setup UI (same as console explain)
    // Parity with the W5500 server: the shared HMI page calls these over WiFi too.
    server.on("/api/sys",   [this]() { send_checked(server,"/api/sys",  [](){ return web_api_sys_json(); }); });
    server.on("/api/login", [this]() { String u=server.arg("user"),p=server.arg("pass"); send_checked(server,"/api/login",[&](){ return web_api_login_json(u.c_str(),p.c_str()); }); });
    server.on("/api/proto", [this]() { String name=server.arg("name"),on=server.arg("on");
        if (name.length()) send_checked(server,"/api/proto",[&](){ return web_api_proto_set_json(name.c_str(),on.c_str()); });
        else               send_checked(server,"/api/proto",[](){ return web_api_proto_json(); }); });
    server.on("/api/reboot",[this]() {
        if (!plc_network_http_allowed(plc_network_ipv4(server.client().remoteIP()), "/api/reboot", true)) { send_json(server, PLC_ACL_ERROR); return; }
        server.send(200, "application/json", web_api_reboot_json()); delay(150); ESP.restart(); });

    server.begin();
    Serial.printf("[Web HMI] Server started on port %d\n", WEB_HMI_PORT);
    return true;
}

void WebHMI::handle() {
    server.handleClient();
}

void WebHMI::handle_root() {
    if (!plc_network_http_allowed(plc_network_ipv4(server.client().remoteIP()), "/")) { send_json(server, PLC_ACL_ERROR); return; }
    server.send(200, "text/html", get_html_page());
}

void WebHMI::handle_coils() {
    const String address = server.arg("addr"), value = server.arg("state");
    send_json(server, web_api_raw_json(true, address.c_str(), server.hasArg("state") ? value.c_str() : nullptr,
                                      plc_network_ipv4(server.client().remoteIP())));
}

void WebHMI::handle_registers() {
    const String address = server.arg("addr"), value = server.arg("value");
    send_json(server, web_api_raw_json(false, address.c_str(), server.hasArg("value") ? value.c_str() : nullptr,
                                      plc_network_ipv4(server.client().remoteIP())));
}

// CWE-22: Path traversal vulnerability in log viewer
// Student can request "/logs?file=../../config.txt" to read arbitrary files
void WebHMI::handle_logs() {
    if (!plc_network_http_allowed(plc_network_ipv4(server.client().remoteIP()), "/logs")) { send_json(server, PLC_ACL_ERROR); return; }
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
    if (!plc_network_http_allowed(plc_network_ipv4(server.client().remoteIP()), "/api/upload")) { send_json(server, PLC_ACL_ERROR); return; }
    // Generic API endpoint (stub)
    server.send(200, "text/plain", "API OK");
}

void WebHMI::handle_points() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    send_json(server,web_api_points_json(plc_network_ipv4(server.client().remoteIP())));
}

void WebHMI::handle_set() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    send_json(server,web_api_set_json(server.arg("tag").c_str(),server.arg("val").c_str(),plc_network_ipv4(server.client().remoteIP())));
}

// Remote network+NTP config, "protected" by a DEFAULT credential (admin:openplc)
// that ships unchanged (CWE-798/1392). An on-path attacker who knows the documented
// default authenticates and repoints the gateway / NTP server - completing the
// rogue-config -> rogue-time AiTM that steps the clock past 2038. The fix is to
// change the default (`netcfg auth <u> <p>`), and to not expose config off-box.
void WebHMI::handle_config() {
    bool allowed;
    { PlcNetworkImageGuard guard;   // D1: hold the image lock only for the ACL check
      allowed = plc_network_http_allowed(plc_network_ipv4(server.client().remoteIP()), "/api/config", server.method() == HTTP_POST); }
    if (!allowed) { send_json(server, PLC_ACL_ERROR); return; }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.authenticate(g_cfg.cfg_user, g_cfg.cfg_pass)) {
        return server.requestAuthentication(BASIC_AUTH, "StamPLC Config",
            "default admin:openplc (CWE-798/1392) - change it with 'netcfg auth'");
    }
    if (server.method() == HTTP_POST) {
        String ip = server.client().remoteIP().toString();
        // apply + commit run with the image lock released (the flash write would jitter the scan)
        String ch = web_config_apply([this](const char* k, char* o, size_t n){
            if (!server.hasArg(k)) return false; server.arg(k).toCharArray(o, n); return true;
        }, ip.c_str());
        server.send(200, "application/json", "{\"saved\":true,\"applied\":\"reboot\",\"changed\":\"" + ch + "\"}");
        return;
    }
    server.send(200, "application/json", web_config_json());
}
void WebHMI::handle_force() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    send_json(server,web_api_force_json(server.arg("tag").c_str(),server.arg("on").c_str(),server.arg("val").c_str(),plc_network_ipv4(server.client().remoteIP())));
}

void WebHMI::handle_mode() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    send_json(server, web_api_mode_json(server.arg("set").c_str(), plc_network_ipv4(server.client().remoteIP())));
}

void WebHMI::handle_hlog() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    send_json(server, web_api_log_json(plc_network_ipv4(server.client().remoteIP())));
}

String WebHMI::get_html_page() { return web_api_html(); }

String web_api_html() {
    // Named-point HMI: view and set the register bits that map to the device's
    // points, like a real PLC/HMI. Data comes from /api/points and /api/log; the
    // "friendly" buttons here and a raw Modbus FC05 write are the SAME action
    // underneath (the CWE-306 lesson). The vulnerable log viewer (CWE-22) is
    // kept below on purpose.
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
