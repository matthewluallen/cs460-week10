#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <cstring>
#include <cstdio>
#include "serial_config.h"
#include "m5stamplc_io.h"
#include <Preferences.h>
DeviceConfig g_cfg;
static Preferences prefs;

void serial_config_load() {
  prefs.begin("cfg", false);
  // ---- WiFi ----
  g_cfg.wifi_mode = prefs.getInt("wifi_mode", WMODE_OFF);     // default OFF = contained
  String s = prefs.getString("ssid", ""); s.toCharArray(g_cfg.ssid, sizeof(g_cfg.ssid));
  String p = prefs.getString("pass", ""); p.toCharArray(g_cfg.pass, sizeof(g_cfg.pass));
  // WiFi addressing: new w_* keys; default DHCP (typical for a WiFi LAN).
  g_cfg.wifi_use_static = prefs.getInt("w_static", 0);
  String wip = prefs.getString("w_ip",   "0.0.0.0");       wip.toCharArray(g_cfg.wifi_ip,   sizeof(g_cfg.wifi_ip));
  String wgw = prefs.getString("w_gw",   "0.0.0.0");       wgw.toCharArray(g_cfg.wifi_gw,   sizeof(g_cfg.wifi_gw));
  String wmk = prefs.getString("w_mask", "255.255.255.0"); wmk.toCharArray(g_cfg.wifi_mask, sizeof(g_cfg.wifi_mask));
  // Setup AP
  String as = prefs.getString("ap_ssid", ""); as.toCharArray(g_cfg.ap_ssid, sizeof(g_cfg.ap_ssid));
  String ap = prefs.getString("ap_pass", ""); ap.toCharArray(g_cfg.ap_pass, sizeof(g_cfg.ap_pass));
  g_cfg.ap_fallback = prefs.getInt("ap_fb", 0);
  // ---- W5500 Ethernet: new e_* keys; migrate from the legacy shared keys if absent ----
  g_cfg.eth_use_static = prefs.getInt("e_static", prefs.getInt("use_static", 1));  // default static (deterministic twin)
  String eip = prefs.getString("e_ip",   prefs.getString("ip",   "10.13.37.10"));  eip.toCharArray(g_cfg.eth_ip,   sizeof(g_cfg.eth_ip));
  String egw = prefs.getString("e_gw",   prefs.getString("gw",   "10.13.37.1"));   egw.toCharArray(g_cfg.eth_gw,   sizeof(g_cfg.eth_gw));
  String emk = prefs.getString("e_mask", prefs.getString("mask", "255.255.255.0"));emk.toCharArray(g_cfg.eth_mask, sizeof(g_cfg.eth_mask));
  // ---- shared ----
  String nt = prefs.getString("ntp", ""); nt.toCharArray(g_cfg.ntp, sizeof(g_cfg.ntp));
  g_cfg.ntp_port = prefs.getInt("ntp_port", 123);
  String cu = prefs.getString("cfg_user", "admin");   cu.toCharArray(g_cfg.cfg_user, sizeof(g_cfg.cfg_user));
  String cp = prefs.getString("cfg_pass", "openplc"); cp.toCharArray(g_cfg.cfg_pass, sizeof(g_cfg.cfg_pass));
  prefs.end();
}

static void save() {
  prefs.begin("cfg", false);
  prefs.putInt("wifi_mode", g_cfg.wifi_mode);
  prefs.putString("ssid", g_cfg.ssid); prefs.putString("pass", g_cfg.pass);
  prefs.putInt("w_static", g_cfg.wifi_use_static);
  prefs.putString("w_ip", g_cfg.wifi_ip); prefs.putString("w_gw", g_cfg.wifi_gw); prefs.putString("w_mask", g_cfg.wifi_mask);
  prefs.putString("ap_ssid", g_cfg.ap_ssid); prefs.putString("ap_pass", g_cfg.ap_pass);
  prefs.putInt("ap_fb", g_cfg.ap_fallback);
  prefs.putInt("e_static", g_cfg.eth_use_static);
  prefs.putString("e_ip", g_cfg.eth_ip); prefs.putString("e_gw", g_cfg.eth_gw); prefs.putString("e_mask", g_cfg.eth_mask);
  prefs.putString("ntp", g_cfg.ntp); prefs.putInt("ntp_port", g_cfg.ntp_port);
  prefs.putString("cfg_user", g_cfg.cfg_user); prefs.putString("cfg_pass", g_cfg.cfg_pass);
  prefs.end();
  Serial.println("[CONFIG] saved.");
}
void serial_config_save() { save(); }

// Effective setup-AP identity. If ap_ssid is unset, derive a stable per-device name from
// the MAC so two boards don't collide; the password falls back to the documented default.
void setup_ap_ssid(char* out, size_t n) {
  if (g_cfg.ap_ssid[0]) { strncpy(out, g_cfg.ap_ssid, n - 1); out[n-1] = 0; return; }
  uint8_t m[6]; esp_read_mac(m, ESP_MAC_WIFI_SOFTAP);
  snprintf(out, n, "%s%02X%02X", SETUP_AP_SSID_PREFIX, m[4], m[5]);
}
const char* setup_ap_pass() { return g_cfg.ap_pass[0] ? g_cfg.ap_pass : SETUP_AP_DEFAULT_PASS; }

static String readLine() {
  String s = "";
  uint32_t start = millis();
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      start = millis();
      if (c == 10 || c == 13) { return s; }
      else s += c;
    }
    if (millis() - start > 30000) return String("\x00");
    delay(5);
  }
}

static void io_selftest() {
  Serial.println("[IOTEST] Read-only sensor/input/output readback.");
  Serial.println("[IOTEST] Output testing requires a force and explicit RUN after boot.");
  for (int k = 0; k < 5; k++) {
    Serial.printf("[IO] temp=%.1fC Vin=%.2fV I=%.3fA | relays=%d%d%d%d | DI=%d%d%d%d%d%d%d%d\n",
      io.temp_read(), io.voltage_read(), io.current_read(),
      io.relay_get(0), io.relay_get(1), io.relay_get(2), io.relay_get(3),
      io.di_read(0), io.di_read(1), io.di_read(2), io.di_read(3),
      io.di_read(4), io.di_read(5), io.di_read(6), io.di_read(7));
    delay(500);
  }
  Serial.println("[IOTEST] done.");
}

static const char* wifi_mode_name(int m) {
  return m == WMODE_STA ? "STA" : (m == WMODE_AP ? "AP-setup" : "OFF (contained)");
}

static void show_cfg() {
  char apn[33]; setup_ap_ssid(apn, sizeof(apn));
  Serial.printf("[CONFIG] WiFi=%s", wifi_mode_name(g_cfg.wifi_mode));
  if (g_cfg.wifi_mode == WMODE_STA)
    Serial.printf(" ssid=%s ip=%s", g_cfg.ssid, g_cfg.wifi_use_static ? g_cfg.wifi_ip : "DHCP");
  else if (g_cfg.wifi_mode == WMODE_AP)
    Serial.printf(" ap=%s (web config on 192.168.4.1:8080)", apn);
  Serial.printf(" | ETH=%s ip=%s\n", g_cfg.eth_use_static ? "static" : "dhcp",
                g_cfg.eth_use_static ? g_cfg.eth_ip : "DHCP");
}

void serial_config_menu(uint32_t window_ms) {
  Serial.println();
  Serial.println("=== StamPLC OpenPLC - Serial Configuration ===");
  Serial.println("This is the deliberately-vulnerable teaching firmware.");
  Serial.println("Default is WiFi OFF (contained). Enable a network only on an ISOLATED segment.");
  show_cfg();
  Serial.printf("[CONFIG] Press ENTER within %lus for the config screen...\n", window_ms / 1000);
  uint32_t start = millis(); bool enter = false;
  while (millis() - start < window_ms) {
    if (Serial.available()) { while (Serial.available()) Serial.read(); enter = true; break; }
    delay(50);
  }
  if (!enter) { Serial.println("[CONFIG] no input - continuing with saved config."); return; }
  while (true) {
    Serial.println();
    Serial.println("  [1] WiFi OFF (contained, default)");
    Serial.println("  [2] WiFi STA  (join a network: SSID/pass)");
    Serial.println("  [9] WiFi AP   (setup mode: host a network + web config UI)");
    Serial.println("  [3] WiFi IP   (DHCP/static for STA)");
    Serial.println("  [0] ETH  IP   (W5500 DHCP/static, if present)");
    Serial.println("  [4] Read-only I/O self-test (sensors/DI/relay readback)");
    Serial.println("  [5] Show embedded CWEs & service ports");
    Serial.println("  [6] Save & boot");
    Serial.println("  [8] Toggle open-AP fallback (VULNERABLE if STA fails)");
    Serial.println("  [7] Factory-reset config");
    Serial.print("Choose: ");
    String c = readLine(); Serial.println(c);
    if (c == String("\x00")) { Serial.println("[CONFIG] menu idle timeout - booting."); return; }
    if (c == "1") { g_cfg.wifi_mode = WMODE_OFF; Serial.println("WiFi OFF."); }
    else if (c == "2") { g_cfg.wifi_mode = WMODE_STA;
      Serial.print("SSID (isolated only): "); readLine().toCharArray(g_cfg.ssid, sizeof(g_cfg.ssid));
      Serial.print("PASS: "); readLine().toCharArray(g_cfg.pass, sizeof(g_cfg.pass)); }
    else if (c == "9") { g_cfg.wifi_mode = WMODE_AP;
      Serial.print("AP SSID (blank=default StampLC-Setup-XXXX): "); readLine().toCharArray(g_cfg.ap_ssid, sizeof(g_cfg.ap_ssid));
      Serial.print("AP PASS (blank=default, >=8 chars): "); readLine().toCharArray(g_cfg.ap_pass, sizeof(g_cfg.ap_pass));
      Serial.println("AP setup mode: connect to the AP, browse http://192.168.4.1:8080 to configure."); }
    else if (c == "3") {
      Serial.print("WiFi static? (y/N): "); String y = readLine(); g_cfg.wifi_use_static = (y == "y" || y == "Y") ? 1 : 0;
      if (g_cfg.wifi_use_static) { Serial.print("IP: "); readLine().toCharArray(g_cfg.wifi_ip, sizeof(g_cfg.wifi_ip));
        Serial.print("GW: "); readLine().toCharArray(g_cfg.wifi_gw, sizeof(g_cfg.wifi_gw));
        Serial.print("MASK: "); readLine().toCharArray(g_cfg.wifi_mask, sizeof(g_cfg.wifi_mask)); } }
    else if (c == "0") {
      Serial.print("ETH static? (Y/n): "); String y = readLine(); g_cfg.eth_use_static = (y == "n" || y == "N") ? 0 : 1;
      if (g_cfg.eth_use_static) { Serial.print("IP: "); readLine().toCharArray(g_cfg.eth_ip, sizeof(g_cfg.eth_ip));
        Serial.print("GW: "); readLine().toCharArray(g_cfg.eth_gw, sizeof(g_cfg.eth_gw));
        Serial.print("MASK: "); readLine().toCharArray(g_cfg.eth_mask, sizeof(g_cfg.eth_mask)); } }
    else if (c == "4") { io_selftest(); }
    else if (c == "5") { Serial.println("CWEs: 798,319,306,494,78,489,330,22 | ports: Modbus 502, HMI 8080, telnet 23"); }
    else if (c == "6") { save(); show_cfg(); return; }
    else if (c == "8") { g_cfg.ap_fallback = !g_cfg.ap_fallback; Serial.println(g_cfg.ap_fallback ? "AP fallback ON (vulnerable open AP)" : "AP fallback OFF (contained)"); }
    else if (c == "7") { prefs.begin("cfg", false); prefs.clear(); prefs.end(); serial_config_load(); Serial.println("reset."); }
  }
}
