// time_sync.h - (insecure) network time client for the StamPLC teaching firmware.
//
// Teaching target for the NTP adversary-in-the-middle + Year-2038 lesson:
//   * the device trusts an UNAUTHENTICATED network time source (CWE-306/CWE-345),
//   * and stores the wall-clock in a signed 32-bit field (CWE-197 / Y2038),
// so an on-path attacker who answers the time query can step the controller's
// clock - or push it past 2038-01-19 03:14:07 UTC so the 32-bit field wraps and
// the `clk` readout (and any wall-clock-derived field) reads 1902. The adverse
// effect is observable, not dangerous; see docs for which records show the wrap.
//
// THREADING: every W5500 socket operation runs on the communications core
// (core 0) inside time_sync_service(), called from comms_task alongside the
// other network services - NEVER from the PLC scan/loop on core 1. The console
// and auto-sync only post a request into a small mailbox (time_sync_request);
// the comms core performs the query and applies the result, so the scan is
// never blocked on the network and the W5500 keeps a single owner.
//
// Transport: real SNTP (48-byte packet) over UDP/123, with a TCP fallback on the
// same port. Guarded by ENABLE_ETHERNET.
#pragma once
#include <stdint.h>
#include <stdbool.h>

void    time_sync_begin();                 // create the mailbox; default source = configured NTP / gateway
void    time_sync_service();               // CORE 0 ONLY (comms_task): runs auto-sync + any posted request

// Post a sync request (safe from any core). Updates the shown source, returns the
// current result sequence; the caller waits for time_sync_result() to advance past it.
uint32_t time_sync_request(const char* server_ip, uint16_t port);
// Read the latest completed result if newer than since_seq. Returns true and fills
// the outputs when a newer result exists. *out32 is the Unix epoch AS STORED
// (truncated to signed 32-bit - the bug); *out64 the correct 64-bit epoch.
bool    time_sync_result(uint32_t since_seq, int32_t* out32, int64_t* out64, bool* ok, bool* via_tcp);

void    time_sync_apply(int64_t unix_secs);// store + settimeofday(). Teaching build truncates to int32 (CWE-197); secure build keeps 64-bit.
bool    time_sync_valid();                 // has a sync happened?
int64_t time_sync_now();                   // current wall-clock (Unix secs) incl. drift
void    time_sync_format(int64_t epoch, char* buf, int buflen);  // "YYYY-MM-DD HH:MM:SS UTC"

// auto-sync config (the "device keeps asking for time" posture the AiTM rides)
void    time_sync_set_server(const char* ip, uint16_t port, uint32_t interval_ms);
const char* time_sync_server();
uint16_t    time_sync_port();
uint32_t    time_sync_interval_ms();
