#include "svc_config.h"
#include <Preferences.h>
#include <cstring>

// delivery: 'D' device service · 'R' RS-485 serial · 'S' sample PCAP + sim · 'M' medium
const SvcDef SVC_DEFS[SVC_COUNT] = {
  {"modbus-tcp",  "SCADA",     502,   'D', true,  "unauth read/write coils (CWE-306) - the live on-board target"},
  {"http-hmi",    "SCADA",     8080,  'D', true,  "web HMI + JSON API, cleartext (CWE-319)"},
  {"telnet",      "legacy",    23,    'D', false, "unauth debug shell (CWE-489); off in the Ethernet build"},
  {"modbus-rtu",  "fieldbus",  0,     'R', true,  "Modbus RTU slave over RS-485, shared I/O image - live serial target"},
  {"mqtt",        "IIoT",      1883,  'D', true,  "northbound publish, authentic Sparkplug B (Tahu); unauth DCMD control"},
  {"dnp3",        "SCADA",     20000, 'D', true,  "on-board outstation (addr 4); CROB DIRECT_OPERATE unauth actuation (CWE-306); SAv5 to defend"},
  {"iec61850",    "substation",102,   'S', false, "MMS/GOOSE/SV; GOOSE spoofing (Industroyer class)"},
  {"iccp",        "control-ctr",102,   'S', false, "TASE.2 / IEC 60870-6 inter-control-center"},
  {"zigbee",      "wireless",  0,     'S', false, "802.15.4 field links; key/replay attacks"},
  {"c37118",      "power/PMU", 4712,  'S', false, "IEEE C37.118 synchrophasor; false-data injection"},
  {"c12-22",      "metering",  1153,  'S', false, "ANSI C12.22 AMI/smart-meter"},
  {"ethernet-ip", "SCADA",     44818, 'D', true,  "on-board identity/fingerprint responder; unauth enumeration (ListIdentity/CIP)"},
  {"s7comm",      "SCADA",     102,   'D', true,  "on-board S7/ISO-TSAP fingerprint (COTP connect + S7 setup); unauth enumeration"},
  {"opcua",       "IIoT",      4840,  'D', true,  "discovery/GetEndpoints responder; SecurityMode None + Anonymous (misconfig)"},
  {"rs232",       "medium",    0,     'M', true,  "physical serial medium - console / field wiring"},
  {"rs485",       "medium",    0,     'M', true,  "multidrop serial bus - carries Modbus RTU"},
  {"iec104",      "SCADA",     2404,  'D', true,  "IEC 60870-5-104 outstation (CA 1); unauth single command (CWE-306); GI + STARTDT"},
  {"bacnet",      "building",  47808, 'D', true,  "BACnet/IP Who-Is/I-Am device responder (UDP); unauth enumeration"},
};

static uint32_t s_flags = 0;
static const uint32_t DEFAULT_FLAGS =
  (1u << SVC_MODBUS_TCP) | (1u << SVC_HTTP_HMI) | (1u << SVC_MQTT);
static const char* NS = "svc";

void svc_begin() {
  Preferences p;
  if (p.begin(NS, true)) { s_flags = p.getUInt("flags", DEFAULT_FLAGS); p.end(); }
  else s_flags = DEFAULT_FLAGS;
}
bool svc_enabled(int i) { return (i >= 0 && i < SVC_COUNT) && (s_flags & (1u << i)); }
bool svc_is_net(int i) {
  return i >= 0 && i < SVC_COUNT && SVC_DEFS[i].delivery == 'D' && SVC_DEFS[i].port != 0;
}
int svc_net_enabled_count() {
  int n = 0;
  for (int i = 0; i < SVC_COUNT; i++) if (svc_enabled(i) && svc_is_net(i)) n++;
  return n;
}
bool svc_set(int i, bool on) {
  if (i < 0 || i >= SVC_COUNT) return false;
  // Enforce the W5500 socket budget: refuse a new network service past the cap.
  if (on && svc_is_net(i) && !svc_enabled(i) && svc_net_enabled_count() >= SVC_MAX_NET)
    return false;
  if (on) s_flags |= (1u << i); else s_flags &= ~(1u << i);
  Preferences p;
  if (p.begin(NS, false)) { p.putUInt("flags", s_flags); p.end(); }
  return true;
}
int svc_find(const char* n) {
  for (int i = 0; i < SVC_COUNT; i++) if (!strcmp(n, SVC_DEFS[i].name)) return i;
  return -1;
}

// W5500 socket age-out (seconds), persisted alongside the service flags. 0 = off.
uint32_t svc_get_sock_timeout_s() {
  Preferences p; uint32_t v = 30;
  if (p.begin(NS, true)) { v = p.getUInt("socktmo", 30); p.end(); }
  return v;
}
void svc_set_sock_timeout_s(uint32_t s) {
  Preferences p;
  if (p.begin(NS, false)) { p.putUInt("socktmo", s); p.end(); }
}

// Saved MQTT broker (numeric IPv4 on the W5500 path). Empty by default: the firmware no
// longer dials a built-in placeholder at boot.
void svc_get_mqtt_broker(char* out, size_t n) {
  if (!out || !n) return;
  out[0] = 0;
  Preferences p;
  if (p.begin(NS, true)) {
    if (p.isKey("mqtt_host")) p.getString("mqtt_host", out, n);
    p.end();
  }
  out[n - 1] = 0;
}
void svc_set_mqtt_broker(const char* host) {
  Preferences p;
  if (!p.begin(NS, false)) return;
  if (host && host[0]) p.putString("mqtt_host", host);
  else if (p.isKey("mqtt_host")) p.remove("mqtt_host");
  p.end();
}
