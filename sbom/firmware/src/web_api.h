// web_api.h - transport-independent HMI/JSON builders shared by the WiFi/lwIP
// WebServer path (web_hmi) and the W5500 Ethernet HTTP server (eth_http).
// One implementation of the point model, so both interfaces stay identical.
#pragma once
#include <Arduino.h>

String web_api_html();                                   // the HMI page (GET /)
String web_api_points_json();                            // GET /api/points
String web_api_log_json();                               // GET /api/log
String web_api_mode_json(const char* set);               // GET /api/mode[?set=run|prog]
String web_api_set_json(const char* tag, const char* val);            // GET /api/set
String web_api_force_json(const char* tag, const char* on, const char* val); // GET /api/force
String web_api_sys_json();                               // GET /api/sys  (device identity / IP banner)
String web_api_login_json(const char* user, const char* pass); // GET /api/login (cleartext creds)
String web_api_reboot_json();                            // GET /api/reboot (remote soft reboot)
String web_api_proto_json();                             // GET /api/proto (list + socket budget)
String web_api_proto_set_json(const char* name, const char* on); // GET /api/proto?name=&on= (cap-enforced)
