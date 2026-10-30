#include <Arduino.h>
#include "config_explain.h"

// Compact teaching strings generated from config-catalog.json (the single source of
// truth in this repo; the richer web/setup renderings read the same file via the export).
// Writing rule (the lab's ethos): `fw` states what the code DOES (observed); `sec` states
// what it MEANS for security (inferred).
struct Explain { const char* key; const char* grp; const char* what; const char* fw; const char* sec; const char* cmd; };

// The rows below are GENERATED from config-catalog.json by
// scripts/generate_config_explain.py (a platformio pre-build step). The catalog
// is the single source of truth; never hand-edit the table - edit the JSON and
// rebuild. Keeping the strings in JSON lets the Studio panel, the /setup page,
// the plc> console and AI tools all teach from the same words.
static const Explain EXPLAIN[] = {
#include "config_explain_table.inc"
};
static const int N_EXPLAIN = sizeof(EXPLAIN)/sizeof(EXPLAIN[0]);

static void print_one(const Explain& e) {
  Serial.printf("%s  (%s)\n  %s\n", e.key, e.grp, e.what);
  if (e.fw[0])  Serial.printf("  firmware: %s\n", e.fw);
  if (e.sec[0]) Serial.printf("  security: %s\n", e.sec);
  if (e.cmd[0]) Serial.printf("  console : %s\n", e.cmd);
}

void config_explain(const char* key) {
  if (!key || !key[0]) {
    Serial.println("Learn a setting: 'explain <name>'   (or 'learn' for a guided walk)");
    Serial.println("Concepts : ip mask gateway dhcp");
    Serial.print("WiFi     : "); for (int i=0;i<N_EXPLAIN;i++) if(!strcmp(EXPLAIN[i].grp,"wifi")) Serial.printf("%s ",EXPLAIN[i].key); Serial.println();
    Serial.print("Ethernet : "); for (int i=0;i<N_EXPLAIN;i++) if(!strcmp(EXPLAIN[i].grp,"ethernet")) Serial.printf("%s ",EXPLAIN[i].key); Serial.println();
    Serial.print("Shared   : "); for (int i=0;i<N_EXPLAIN;i++) if(!strcmp(EXPLAIN[i].grp,"shared")) Serial.printf("%s ",EXPLAIN[i].key); Serial.println();
    return;
  }
  for (int i=0;i<N_EXPLAIN;i++) if (!strcmp(EXPLAIN[i].key,key)) { print_one(EXPLAIN[i]); return; }
  Serial.printf("no explanation for '%s'. Type 'explain' for the list.\n", key);
}

void config_learn() {
  Serial.println("=== StampLC configuration, explained ===");
  Serial.println("Settings apply to two interfaces: WiFi (always) and the W5500 Ethernet (if fitted).");
  Serial.println("Each has its own address, set automatically (DHCP) or fixed (Static).\n");
  const char* groups[] = {"concept","wifi","ethernet","shared"};
  const char* titles[] = {"-- concepts --","-- WiFi --","-- Ethernet (W5500) --","-- shared --"};
  for (int g=0; g<4; g++) { Serial.printf("%s\n", titles[g]);
    for (int i=0;i<N_EXPLAIN;i++) if(!strcmp(EXPLAIN[i].grp,groups[g])) { print_one(EXPLAIN[i]); Serial.println(); } }
  Serial.println("Set anything with the 'netcfg' commands shown, then reboot. 'netcfg json' reads the live state.");
}

String config_explain_json_str() {
  String j = "{\"version\":1,\"items\":[";
  for (int i=0;i<N_EXPLAIN;i++) {
    j += (i?",":"");
    j += "{\"k\":\""; j+=EXPLAIN[i].key;
    j += "\",\"grp\":\""; j+=EXPLAIN[i].grp;
    j += "\",\"what\":\""; j+=EXPLAIN[i].what;
    j += "\",\"fw\":\""; j+=EXPLAIN[i].fw;
    j += "\",\"sec\":\""; j+=EXPLAIN[i].sec;
    j += "\",\"cmd\":\""; j+=EXPLAIN[i].cmd; j += "\"}";
  }
  j += "]}"; return j;
}
void config_explain_json() { Serial.println(config_explain_json_str()); }
