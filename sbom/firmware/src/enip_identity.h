#ifndef ENIP_IDENTITY_H
#define ENIP_IDENTITY_H
#include <Arduino.h>
#include "net_types.h"
#include "config.h"

// EtherNet/IP identity/fingerprint responder — on-device enumeration target (LS-20).
// Answers the two requests a scanner (pycomm3/cpppo/nmap-enip, Wireshark) uses to
// FINGERPRINT an OT device on TCP 44818: RegisterSession, ListIdentity, and a CIP
// GetAttributesAll on the Identity object (class 1). The device over-discloses a
// full identity with no authentication (information disclosure — the enumeration
// lesson). Teaching identity only (no real-vendor impersonation).
class EnipIdentity {
public:
    EnipIdentity();
    bool start();
    void handle();
private:
    NetServer server;
    int identity_core(uint8_t* b);                    // vendor..product-name (shared)
    int list_identity_reply(uint8_t* out, const uint8_t* ctx);
    int register_session_reply(uint8_t* out, const uint8_t* ctx);
    int sendrrdata_identity_reply(uint8_t* out, uint32_t session, const uint8_t* ctx);
    char cur_src_ip[24];
};
extern EnipIdentity enip_identity;
#endif // ENIP_IDENTITY_H
