#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H
#include <Arduino.h>
struct DeviceConfig {
  int  wifi_mode;      // 0 = OFF (contained default), 1 = STA
  char ssid[33];
  char pass[65];
  int  use_static;     // 0 = DHCP, 1 = static (default static: deterministic in the twin)
  char ip[16]; char gw[16]; char mask[16];
  int  ap_fallback;   // 0=off (contained), 1=vulnerable open-AP fallback
  char ntp[16];       // network time source (empty -> use gateway); NTP AiTM / Y2038 target
  int  ntp_port;      // default 123
  char cfg_user[24];  // remote-config account (CWE-1392 default: admin)
  char cfg_pass[24];  // remote-config password (CWE-798/1392 default: openplc - UNCHANGED in the field)
};
extern DeviceConfig g_cfg;
extern bool g_net_up;
void serial_config_load();
void serial_config_menu(uint32_t window_ms);
void serial_config_save();   // persist g_cfg to NVS (used by the `netcfg` console cmd + /api/config)
#endif
