#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include "serial_config.h"
#include "m5stamplc_io.h"
#include <Preferences.h>
DeviceConfig g_cfg;
static Preferences prefs;

void serial_config_load() {
  prefs.begin("cfg", false);
  g_cfg.wifi_mode = prefs.getInt("wifi_mode", 0);          // default OFF = contained
  String s = prefs.getString("ssid", ""); s.toCharArray(g_cfg.ssid, sizeof(g_cfg.ssid));
  String p = prefs.getString("pass", ""); p.toCharArray(g_cfg.pass, sizeof(g_cfg.pass));
  g_cfg.use_static = prefs.getInt("use_static", 1);   // default STATIC: deterministic twin boot
  String ip = prefs.getString("ip", "10.13.37.10"); ip.toCharArray(g_cfg.ip, sizeof(g_cfg.ip));
  String gw = prefs.getString("gw", "10.13.37.1");  gw.toCharArray(g_cfg.gw, sizeof(g_cfg.gw));
  String mk = prefs.getString("mask", "255.255.255.0"); mk.toCharArray(g_cfg.mask, sizeof(g_cfg.mask));
  g_cfg.ap_fallback = prefs.getInt("ap_fb", 0);
  String nt = prefs.getString("ntp", ""); nt.toCharArray(g_cfg.ntp, sizeof(g_cfg.ntp));   // empty -> gateway
  g_cfg.ntp_port = prefs.getInt("ntp_port", 123);
  String cu = prefs.getString("cfg_user", "admin");   cu.toCharArray(g_cfg.cfg_user, sizeof(g_cfg.cfg_user));
  String cp = prefs.getString("cfg_pass", "openplc"); cp.toCharArray(g_cfg.cfg_pass, sizeof(g_cfg.cfg_pass)); // CWE-798/1392 default
  prefs.end();
}

static void save() {
  prefs.begin("cfg", false);
  prefs.putInt("wifi_mode", g_cfg.wifi_mode);
  prefs.putString("ssid", g_cfg.ssid); prefs.putString("pass", g_cfg.pass);
  prefs.putInt("use_static", g_cfg.use_static);
  prefs.putString("ip", g_cfg.ip); prefs.putString("gw", g_cfg.gw); prefs.putString("mask", g_cfg.mask);
  prefs.putInt("ap_fb", g_cfg.ap_fallback);
  prefs.putString("ntp", g_cfg.ntp); prefs.putInt("ntp_port", g_cfg.ntp_port);
  prefs.putString("cfg_user", g_cfg.cfg_user); prefs.putString("cfg_pass", g_cfg.cfg_pass);
  prefs.end();
  Serial.println("[CONFIG] saved.");
}
void serial_config_save() { save(); }

static String readLine() {
  String s = "";
  uint32_t start = millis();
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      start = millis();  // reset idle timer on any keypress
      if (c == 10 || c == 13) { return s; }   // ENTER ends the line (empty allowed)
      else s += c;
    }
    if (millis() - start > 30000) return String("\x00");  // 30s idle -> sentinel
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

static void show_cfg() {
  Serial.printf("[CONFIG] WiFi=%s", g_cfg.wifi_mode ? "STA" : "OFF (contained)");
  if (g_cfg.wifi_mode) Serial.printf(" ssid=%s ip=%s", g_cfg.ssid, g_cfg.use_static ? g_cfg.ip : "DHCP");
  Serial.println();
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
    Serial.println("  [2] WiFi STA  (set SSID/pass)");
    Serial.println("  [3] IP mode   (DHCP/static, default static 10.13.37.10)");
    Serial.println("  [4] Read-only I/O self-test (sensors/DI/relay readback)");
    Serial.println("  [5] Show embedded CWEs & service ports");
    Serial.println("  [6] Save & boot");
    Serial.println("  [8] Toggle open-AP fallback (VULNERABLE if STA fails)");
    Serial.println("  [7] Factory-reset config");
    Serial.print("Choose: ");
    String c = readLine(); Serial.println(c);
    if (c == String("\x00")) { Serial.println("[CONFIG] menu idle timeout - booting with current config."); return; }
    if (c == "1") { g_cfg.wifi_mode = 0; Serial.println("WiFi OFF."); }
    else if (c == "2") { g_cfg.wifi_mode = 1;
      Serial.print("SSID (isolated only): "); readLine().toCharArray(g_cfg.ssid, sizeof(g_cfg.ssid));
      Serial.print("PASS: "); readLine().toCharArray(g_cfg.pass, sizeof(g_cfg.pass)); }
    else if (c == "3") {
      Serial.print("static? (y/N): "); String y = readLine(); g_cfg.use_static = (y == "y" || y == "Y") ? 1 : 0;
      if (g_cfg.use_static) { Serial.print("IP: "); readLine().toCharArray(g_cfg.ip, sizeof(g_cfg.ip));
        Serial.print("GW: "); readLine().toCharArray(g_cfg.gw, sizeof(g_cfg.gw));
        Serial.print("MASK: "); readLine().toCharArray(g_cfg.mask, sizeof(g_cfg.mask)); } }
    else if (c == "4") { io_selftest(); }
    else if (c == "5") { Serial.println("CWEs: 798,319,306,494,78,489,330,22 | ports: Modbus 502, HMI 8080, telnet 23"); }
    else if (c == "6") { save(); show_cfg(); return; }
    else if (c == "8") { g_cfg.ap_fallback = !g_cfg.ap_fallback; Serial.println(g_cfg.ap_fallback ? "AP fallback ON (vulnerable open AP)" : "AP fallback OFF (contained)"); }
    else if (c == "7") { prefs.begin("cfg", false); prefs.clear(); prefs.end(); serial_config_load(); Serial.println("reset."); }
  }
}
