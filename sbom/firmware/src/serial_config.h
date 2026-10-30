#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H
#include <Arduino.h>

// Per-interface network configuration.
//
// The StampLC has TWO independent interfaces:
//   • WiFi   — always present on the ESP32-S3. Modes: OFF / STA (join a network) /
//              AP (host a setup network so the device can be provisioned over WiFi).
//   • W5500  — the PoE Ethernet module, present only when installed. Configured only
//              when detected (eth_present()).
// Each interface has its own addressing (static ip/gw/mask or DHCP), so the W5500 can
// sit on a fixed plant subnet while WiFi takes DHCP on a separate network — what a real
// dual-homed PLC does. Shared across both: NTP source and the remote-config account.
enum WifiMode { WMODE_OFF = 0, WMODE_STA = 1, WMODE_AP = 2 };

struct DeviceConfig {
  // ---- WiFi (always present) ----
  int  wifi_mode;         // WifiMode: 0=OFF, 1=STA, 2=AP (setup/provisioning)
  char ssid[33];          // STA: network to join
  char pass[65];
  int  wifi_use_static;   // STA addressing: 0=DHCP, 1=static
  char wifi_ip[16]; char wifi_gw[16]; char wifi_mask[16];
  // Setup AP (wifi_mode==AP): the device hosts this network and serves the web config
  // UI on it, so a phone/laptop can configure the device without USB. A DEFAULT ssid/pass
  // is a deliberate CWE-798/1392 teaching point — real deployments must change it.
  char ap_ssid[33];       // empty -> bring-up derives "StampLC-Setup-XXXX" from the MAC
  char ap_pass[65];       // empty -> bring-up uses the documented default (>=8 chars, WPA2)
  int  ap_fallback;       // legacy: on STA-connect failure, fall back to an OPEN AP (vulnerable)

  // ---- W5500 Ethernet (only when present) ----
  int  eth_use_static;    // 0=DHCP, 1=static
  char eth_ip[16]; char eth_gw[16]; char eth_mask[16];

  // ---- shared ----
  char ntp[16];           // network time source (empty -> use the active gateway)
  int  ntp_port;          // default 123
  char cfg_user[24];      // remote-config account (CWE-1392 default: admin)
  char cfg_pass[24];      // remote-config password (CWE-798/1392 default: openplc)
};
extern DeviceConfig g_cfg;
extern bool g_net_up;

// Default setup-AP credentials (documented; deliberately weak for the teaching lab).
#define SETUP_AP_SSID_PREFIX "StampLC-Setup-"
#define SETUP_AP_DEFAULT_PASS "stamplc-setup"

void serial_config_load();
void serial_config_menu(uint32_t window_ms);
void serial_config_save();   // persist g_cfg to NVS (used by `netcfg`, /api/config, setup web UI)
// Compute the effective setup-AP SSID (ap_ssid, or a MAC-suffixed default into out).
void setup_ap_ssid(char* out, size_t n);
const char* setup_ap_pass();
#endif
