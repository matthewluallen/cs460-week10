#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H
#include <Arduino.h>
#include <functional>
// Shared, transport-independent device network-config surface, used by BOTH HTTP
// servers (the W5500 eth_http.cpp and the WiFi/lwIP web_hmi.cpp) and mirrored by the
// console 'netcfg json'. Having ONE source for the per-interface config JSON + the
// field-apply means the two interfaces can no longer drift — which they had
// (different NTP-apply timing, a triplicated JSON that gained an rssi field in only
// one copy). Each server passes a small key-value getter wrapping its own arg parser.
String web_config_json();                                   // per-interface eth/wifi/ntp/cfg JSON (+rssi)
using WebKvGetter = std::function<bool(const char* key, char* out, size_t n)>;
String web_config_apply(const WebKvGetter& get, const char* client_ip, bool commit = true);  // sets g_cfg fields (fast); returns the change list
void   web_config_commit();   // the SLOW part: persist to flash + apply NTP. Call OUTSIDE the PLC image lock (a multi-ms flash write under it injects scan jitter).
#endif
