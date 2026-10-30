#ifndef CONFIG_EXPLAIN_H
#define CONFIG_EXPLAIN_H
#include <Arduino.h>
// Console teaching for the configuration settings — the USB-exploration surface of the
// educational-config design (one catalog -> four surfaces). The strings here are the
// COMPACT form, aligned with the rich web/public/config-catalog.json; a generator should
// emit this table from that JSON so code and UI can never drift (see the design doc).
//   explain              list the settings a newcomer can learn about
//   explain <key>        teach one setting: what it is, what the firmware does, the risk
//   learn                a short guided walk through every setting
//   (netcfg explain json emits the same table machine-readably for AI tools)
void config_explain(const char* key);     // key==nullptr/"" -> list
void config_learn();
void config_explain_json();
String config_explain_json_str();   // same table as a String (for the web /api/explain route)
extern const char SETUP_PAGE[];      // on-device /setup page (defined in web_hmi.cpp), served over WiFi and the wire
#endif
