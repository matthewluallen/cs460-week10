// eth_net.h - physical W5500 Ethernet (StamPLC PoE module), using
// Ethernet_Generic and the chip's hardware TCP/IP stack. net_types.h selects
// EthernetServer/Client; this path does not use ETH.h or the WiFi/lwIP stack.
// SPI2: SCK=7 MISO=9 MOSI=8 CS=11 (shared with the LCD bus). Guarded by ENABLE_ETHERNET.
#pragma once
#include <Arduino.h>
bool   eth_begin(const char* ip, const char* gw, const char* mask, bool use_static = true);
bool   eth_up();
bool   eth_ok();   // W5500 hardware detected
void   eth_prepare_shared_int();  // mask PI4IOE INT on shared G14 before eth_begin
String eth_ip();
void   eth_dump_sockets();
int    eth_listen_count(uint16_t port = 0); // 0 counts all ports
// --- socket age-out reaper (frees half-open / idle W5500 sockets) ---
void     eth_reap_sockets();               // call periodically from the comms loop
void     eth_set_sock_timeout_ms(uint32_t ms);  // idle age-out; 0 disables the reaper
uint32_t eth_get_sock_timeout_ms();
uint32_t eth_reaped_count();               // sockets force-closed by the reaper since boot
void     eth_reap_exempt(int sock);        // an outbound client socket with its own keepalive (-1: none)
