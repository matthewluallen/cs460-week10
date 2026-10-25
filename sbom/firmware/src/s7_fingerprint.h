#ifndef S7_FINGERPRINT_H
#define S7_FINGERPRINT_H
#include <Arduino.h>
#include "net_types.h"
#include "config.h"

// Siemens S7 / ISO-TSAP fingerprint responder — on-device enumeration target (LS-20).
// Presents as a Siemens CPU 315-2 PN/DP (the identity Snap7 and Conpot honeypots
// emulate) on TCP 102: COTP Connection Request -> Confirm, S7 Communication Setup,
// and SZL (System Status List) reads 0x0011 (module identification) + 0x001C
// (component identification) laid out to nmap s7-info's exact parse offsets. No
// authentication (the enumeration lesson). ISOLATED LAB HONEYPOT ONLY - it names
// a real order number so students can pivot to CISA advisories; do not deploy it
// where a scanner could mistake it for a production PLC.
class S7Fingerprint {
public:
    S7Fingerprint();
    bool start();
    void handle();
private:
    NetServer server;
    int build_szl(uint8_t* out, uint16_t seq, uint16_t szl_id);
    char cur_src_ip[24];
};
extern S7Fingerprint s7_fingerprint;
#endif // S7_FINGERPRINT_H
