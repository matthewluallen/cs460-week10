// svc_config.h — TBD protocol catalog + selectable device services (LS-17).
// Covers the full Lab-3 protocol set from the course outline. Each entry carries a
// DELIVERY class: how the protocol is exercised in the lab —
//   'D' device service (real, gate-able on the StamPLC), 'R' RS-485 serial,
//   'S' sample PCAP + gateway simulator (inspect/attack/defend off-box),
//   'M' physical medium (RS-232/RS-485/ZigBee RF).
// Only 'D' services actually open a port on the board; the rest are catalog +
// lab-environment entries so students inspect, attack and defend them via the
// sample environment (PCAPs + sims + IDS signatures).
#pragma once
#include <Arduino.h>

enum {
  SVC_MODBUS_TCP = 0, SVC_HTTP_HMI, SVC_TELNET,   // (first three fixed: main.cpp gates these)
  SVC_MODBUS_RTU, SVC_MQTT,
  SVC_DNP3, SVC_IEC61850, SVC_ICCP, SVC_ZIGBEE, SVC_C37118, SVC_C12_22,
  SVC_ENIP, SVC_S7, SVC_OPCUA,
  SVC_RS232, SVC_RS485,
  SVC_IEC104,
  SVC_BACNET,
  SVC_COUNT
};

// Socket budget: the W5500 has only 8 hardware sockets shared by every network
// service, so cap how many can be enabled at once. A user cannot turn on more
// than this many network protocols (they must disable one first).
#define SVC_MAX_NET 4   // hard cap: never more than 4 concurrent network services (W5500 8-socket budget)

struct SvcDef { const char* name; const char* group; uint16_t port; char delivery; bool implemented; const char* note; };
extern const SvcDef SVC_DEFS[SVC_COUNT];

void svc_begin();
bool svc_enabled(int i);
bool svc_set(int i, bool on);            // false if refused (over the SVC_MAX_NET budget)
bool svc_is_net(int i);                  // true if the service consumes a W5500 socket
int  svc_net_enabled_count();            // # enabled network (socket) services
int  svc_find(const char* name);
// MQTT broker the controller dials at boot (set by `mqtt connect`, cleared by `mqtt
// disconnect`). Empty = no broker configured = nothing is dialed. Persisted in NVS "svc".
void svc_get_mqtt_broker(char* out, size_t n);
void svc_set_mqtt_broker(const char* host);   // nullptr or "" clears it
uint32_t svc_get_sock_timeout_s();       // W5500 socket age-out (s); 0 disables
void     svc_set_sock_timeout_s(uint32_t s);
