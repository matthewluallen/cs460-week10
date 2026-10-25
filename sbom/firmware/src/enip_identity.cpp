#include "enip_identity.h"
#include "plc_core.h"          // plc_log / SRC_MODBUS
#include <string.h>

EnipIdentity enip_identity;

// ---- teaching identity (CIP Identity object). Not a registered ODVA vendor; the
//      strings name the course platform so students recognise what they enumerated.
static const char  ENIP_PRODUCT_NAME[] = "CYBATI StamPLC OpenPLC";
static const uint16_t ENIP_VENDOR_ID   = 0x0533;   // teaching value (not Rockwell/real)
static const uint16_t ENIP_DEVICE_TYPE = 0x000E;   // CIP profile: programmable logic controller
static const uint16_t ENIP_PRODUCT_CODE= 0x00CE;
static const uint8_t  ENIP_REV_MAJOR   = 1, ENIP_REV_MINOR = 3;
static const uint16_t ENIP_STATUS      = 0x0060;   // owned + configured (typical)
static const uint32_t ENIP_SERIAL      = 0x0C5C0E19;
static const uint16_t ENIP_SESSION     = 0x00000001;

// little-endian helpers
static inline void w16(uint8_t*& p, uint16_t v){ *p++=v&0xFF; *p++=v>>8; }
static inline void w32(uint8_t*& p, uint32_t v){ *p++=v&0xFF; *p++=(v>>8)&0xFF; *p++=(v>>16)&0xFF; *p++=v>>24; }

// 24-byte EtherNet/IP encapsulation header + payload.
static int enip_encap(uint8_t* out, uint16_t cmd, uint32_t session, uint32_t status,
                      const uint8_t* ctx, const uint8_t* data, int dlen) {
    uint8_t* p = out;
    w16(p, cmd); w16(p, (uint16_t)dlen); w32(p, session); w32(p, status);
    if (ctx) { memcpy(p, ctx, 8); } else { memset(p, 0, 8); } p += 8;
    w32(p, 0);                                   // options
    if (dlen) { memcpy(p, data, dlen); p += dlen; }
    return (int)(p - out);
}

// Identity core: vendor..product-name (as GetAttributesAll of the Identity object).
int EnipIdentity::identity_core(uint8_t* b) {
    uint8_t* p = b;
    w16(p, ENIP_VENDOR_ID);
    w16(p, ENIP_DEVICE_TYPE);
    w16(p, ENIP_PRODUCT_CODE);
    *p++ = ENIP_REV_MAJOR; *p++ = ENIP_REV_MINOR;
    w16(p, ENIP_STATUS);
    w32(p, ENIP_SERIAL);
    uint8_t nl = (uint8_t)strlen(ENIP_PRODUCT_NAME);
    *p++ = nl; memcpy(p, ENIP_PRODUCT_NAME, nl); p += nl;
    return (int)(p - b);
}

// ListIdentity reply: CPF with one CIP Identity item (type 0x000C).
int EnipIdentity::list_identity_reply(uint8_t* out, const uint8_t* ctx) {
    uint8_t item[128]; uint8_t* p = item;
    w16(p, 1);                                   // encapsulation protocol version
    // socket address (network byte order): family, port, addr, zero(8)
    *p++ = 0x00; *p++ = 0x02;                    // AF_INET
    *p++ = (ENIP_TCP_PORT >> 8) & 0xFF; *p++ = ENIP_TCP_PORT & 0xFF;
    memset(p, 0, 4); p += 4;                     // sin_addr 0.0.0.0
    memset(p, 0, 8); p += 8;                     // sin_zero
    p += identity_core(p);
    *p++ = 0xFF;                                 // state (0xFF = not present/idle)
    int ilen = (int)(p - item);

    uint8_t data[160]; uint8_t* d = data;
    w16(d, 1);                                   // CPF item count
    w16(d, 0x000C);                              // item type: List Identity
    w16(d, (uint16_t)ilen);
    memcpy(d, item, ilen); d += ilen;
    return enip_encap(out, 0x0063, 0, 0, ctx, data, (int)(d - data));
}

int EnipIdentity::register_session_reply(uint8_t* out, const uint8_t* ctx) {
    uint8_t d[4]; uint8_t* p = d; w16(p, 1); w16(p, 0);   // protocol version 1, options 0
    return enip_encap(out, 0x0065, ENIP_SESSION, 0, ctx, d, 4);
}

// SendRRData reply carrying a CIP GetAttributesAll success on the Identity object.
int EnipIdentity::sendrrdata_identity_reply(uint8_t* out, uint32_t session, const uint8_t* ctx) {
    uint8_t cip[128]; uint8_t* c = cip;
    *c++ = 0x81; *c++ = 0x00; *c++ = 0x00; *c++ = 0x00;   // service reply 0x01|0x80, status ok
    c += identity_core(c);

    uint8_t data[160]; uint8_t* d = data;
    w32(d, 0); w16(d, 0);                        // interface handle, timeout
    w16(d, 2);                                   // CPF item count
    w16(d, 0x0000); w16(d, 0);                   // null address item
    w16(d, 0x00B2); w16(d, (uint16_t)(c - cip)); // unconnected data item + length
    memcpy(d, cip, c - cip); d += (c - cip);
    return enip_encap(out, 0x006F, session, 0, ctx, data, (int)(d - data));
}

EnipIdentity::EnipIdentity() : server(ENIP_TCP_PORT) { cur_src_ip[0] = 0; }

bool EnipIdentity::start() {
    for (int i = 0; i < 3; i++) { server.begin(); delay(20); }  // 3x = boot retry robustness (begin() is best-effort); available() maintains one listener thereafter
    Serial.printf("[ENIP] EtherNet/IP identity responder on TCP %d - unauth enumeration target\n", ENIP_TCP_PORT);
    return true;
}

void EnipIdentity::handle() {
    NetClient client = server.available();
    if (!client) return;
    if (!client.connected()) { client.stop(); return; }
    if (!client.available()) return;

    uint8_t req[300];
    size_t want = client.available(); if (want > sizeof(req)) want = sizeof(req);
    int len = client.readBytes(req, want);
    if (len < 24) return;                        // shorter than an encapsulation header

    client.remoteIP().toString().toCharArray(cur_src_ip, sizeof(cur_src_ip));
    uint16_t cmd     = (uint16_t)req[0] | ((uint16_t)req[1] << 8);
    uint32_t session = (uint32_t)req[4] | ((uint32_t)req[5] << 8) | ((uint32_t)req[6] << 16) | ((uint32_t)req[7] << 24);
    const uint8_t* ctx = req + 12;               // sender context (echoed back)

    uint8_t out[300]; int ol = 0;
    switch (cmd) {
        case 0x0063: ol = list_identity_reply(out, ctx);
                     plc_log(SRC_ENIP, cur_src_ip, "EtherNet/IP ListIdentity (device enumeration)"); break;
        case 0x0065: ol = register_session_reply(out, ctx);
                     plc_log(SRC_ENIP, cur_src_ip, "EtherNet/IP RegisterSession"); break;
        case 0x006F: ol = sendrrdata_identity_reply(out, session, ctx);
                     plc_log(SRC_ENIP, cur_src_ip, "EtherNet/IP SendRRData (CIP Identity read)"); break;
        default: return;
    }
    if (ol > 0) client.write(out, ol);
}
