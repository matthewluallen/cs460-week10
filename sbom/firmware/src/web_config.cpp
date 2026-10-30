#include "web_config.h"
#include <WiFi.h>
#include <string.h>
#include <stdlib.h>
#include "serial_config.h"
#include "eth_net.h"
#include "time_sync.h"
#include "plc_core.h"      // plc_log, SRC_WEB

static void set_str(char* d, size_t n, const char* s) { strncpy(d, s, n - 1); d[n - 1] = 0; }

String web_config_json() {
    char apn[33]; setup_ap_ssid(apn, sizeof(apn));
    bool present = eth_ok();
    String wmode = g_cfg.wifi_mode==WMODE_STA?"sta":(g_cfg.wifi_mode==WMODE_AP?"ap":"off");
    String wlink = (g_cfg.wifi_mode==WMODE_AP)?WiFi.softAPIP().toString():(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("no-net"));
    String j = "{\"eth\":{\"present\":"; j += present?"true":"false";
    j += ",\"mode\":\""; j += g_cfg.eth_use_static?"static":"dhcp";
    j += "\",\"ip\":\""+String(g_cfg.eth_ip)+"\",\"gw\":\""+String(g_cfg.eth_gw)+"\",\"mask\":\""+String(g_cfg.eth_mask)+"\",\"link\":\""+(present?eth_ip():String("absent"))+"\"},";
    j += "\"wifi\":{\"mode\":\""+wmode+"\",\"ssid\":\""+String(g_cfg.ssid)+"\",\"ap_ssid\":\""+String(apn)+"\",\"addr\":\""+String(g_cfg.wifi_use_static?"static":"dhcp")+"\",\"ip\":\""+String(g_cfg.wifi_ip)+"\",\"gw\":\""+String(g_cfg.wifi_gw)+"\",\"mask\":\""+String(g_cfg.wifi_mask)+"\",\"link\":\""+wlink+"\",\"rssi\":"+String((int)WiFi.RSSI())+"},";
    j += "\"ntp\":\""+String(g_cfg.ntp[0]?g_cfg.ntp:g_cfg.eth_gw)+"\",\"ntp_port\":"+String(g_cfg.ntp_port)+",\"cfg_user\":\""+String(g_cfg.cfg_user)+"\"}";
    return j;
}

String web_config_apply(const WebKvGetter& get, const char* client_ip, bool commit) {
    char v[80]; String ch = "";
    auto has = [&](const char* k){ return get(k, v, sizeof(v)); };
    // Ethernet (eth_* preferred; legacy mode/ip/gw/mask also map to eth)
    if (has("eth_mode") || has("mode")) { g_cfg.eth_use_static = !strcmp(v,"static"); ch += "eth_mode "; }
    if (has("eth_ip")   || has("ip"))   { set_str(g_cfg.eth_ip,   sizeof(g_cfg.eth_ip),   v); ch += "eth_ip "; }
    if (has("eth_gw")   || has("gw"))   { set_str(g_cfg.eth_gw,   sizeof(g_cfg.eth_gw),   v); ch += "eth_gw "; }
    if (has("eth_mask") || has("mask")) { set_str(g_cfg.eth_mask, sizeof(g_cfg.eth_mask), v); ch += "eth_mask "; }
    // WiFi
    if (has("wifi_mode")) { g_cfg.wifi_mode = !strcmp(v,"sta")?WMODE_STA:(!strcmp(v,"ap")?WMODE_AP:WMODE_OFF); ch += "wifi_mode "; }
    if (has("ssid"))      { set_str(g_cfg.ssid, sizeof(g_cfg.ssid), v); ch += "ssid "; }
    if (has("pass") && v[0]) { set_str(g_cfg.pass, sizeof(g_cfg.pass), v); ch += "pass "; }
    if (has("wifi_addr")) { g_cfg.wifi_use_static = !strcmp(v,"static"); ch += "wifi_addr "; }
    if (has("wifi_ip"))   { set_str(g_cfg.wifi_ip,   sizeof(g_cfg.wifi_ip),   v); ch += "wifi_ip "; }
    if (has("wifi_gw"))   { set_str(g_cfg.wifi_gw,   sizeof(g_cfg.wifi_gw),   v); ch += "wifi_gw "; }
    if (has("wifi_mask")) { set_str(g_cfg.wifi_mask, sizeof(g_cfg.wifi_mask), v); ch += "wifi_mask "; }
    if (has("ap_ssid"))   { set_str(g_cfg.ap_ssid, sizeof(g_cfg.ap_ssid), v); ch += "ap_ssid "; }
    if (has("ap_pass") && v[0]) { set_str(g_cfg.ap_pass, sizeof(g_cfg.ap_pass), v); ch += "ap_pass "; }
    // shared
    if (has("ntp"))      { set_str(g_cfg.ntp, sizeof(g_cfg.ntp), v); ch += "ntp "; }
    if (has("ntp_port")) { g_cfg.ntp_port = atoi(v); ch += "ntp_port "; }
    if (has("cfg_user")) { set_str(g_cfg.cfg_user, sizeof(g_cfg.cfg_user), v); ch += "cfg_user "; }
    if (has("cfg_pass")) { set_str(g_cfg.cfg_pass, sizeof(g_cfg.cfg_pass), v); ch += "cfg_pass "; }
    plc_log(SRC_WEB, client_ip, "REMOTE netcfg via /api/config: %s", ch.c_str());
    if (commit) web_config_commit();   // caller may defer this to outside the image lock
    return ch;
}

void web_config_commit() {
    serial_config_save();              // NVS/flash write (tens of ms) — never hold the image lock here
    time_sync_set_server(g_cfg.ntp[0] ? g_cfg.ntp : g_cfg.eth_gw, (uint16_t)g_cfg.ntp_port, time_sync_interval_ms());
}
