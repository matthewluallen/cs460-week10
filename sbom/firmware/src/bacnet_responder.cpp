#include "diagnostic_log.h"
#include "bacnet_responder.h"
#include "plc_core.h"          // plc_log / SRC_BACNET
#include <string.h>

BacnetResponder bacnet_responder;
BacnetResponder::BacnetResponder() { cur_src_ip[0] = 0; }

#ifdef ENABLE_ETHERNET
#include <Ethernet_Generic.hpp>

// ---- teaching identity (not a registered ASHRAE vendor). ----
static const uint32_t BACNET_DEVICE_INSTANCE = 1013710;  // teaching value (echoes 10.13.37.10); 22-bit
static const uint16_t BACNET_VENDOR_ID       = 1976;     // teaching value; NOT a real registered vendor
static const uint16_t BACNET_MAX_APDU        = 1476;     // typical BACnet/IP max APDU

static EthernetUDP udp;

static bool s_bound = false;

bool BacnetResponder::start() {
    s_bound = (udp.begin(BACNET_PORT) != 0);   // may fail if the socket pool is momentarily full
    Serial.printf("[BACnet] BACnet/IP responder on UDP %d (Who-Is -> I-Am)%s - unauth enumeration target\n",
                  BACNET_PORT, s_bound ? "" : " [waiting for a free socket]");
    return true;
}

// Encode an I-Am APDU (device object identity) into out; returns length.
static int build_iam(uint8_t* out) {
    uint8_t* p = out;
    *p++ = 0x10;                                   // Unconfirmed-Request PDU
    *p++ = 0x00;                                   // service choice: I-Am
    *p++ = 0xC4;                                   // app tag 12 (ObjectIdentifier), len 4
    uint32_t objid = ((uint32_t)8 << 22) | (BACNET_DEVICE_INSTANCE & 0x3FFFFF);  // type 8 = device
    *p++ = (objid >> 24) & 0xFF; *p++ = (objid >> 16) & 0xFF;
    *p++ = (objid >> 8)  & 0xFF; *p++ =  objid        & 0xFF;
    *p++ = 0x22; *p++ = (BACNET_MAX_APDU >> 8) & 0xFF; *p++ = BACNET_MAX_APDU & 0xFF;  // max APDU (unsigned,2)
    *p++ = 0x91; *p++ = 0x03;                      // segmentation supported = 3 (no segmentation)
    *p++ = 0x22; *p++ = (BACNET_VENDOR_ID >> 8) & 0xFF; *p++ = BACNET_VENDOR_ID & 0xFF; // vendor id (unsigned,2)
    return (int)(p - out);
}

void BacnetResponder::handle() {
    if (!s_bound) {                              // pool was full at boot; retry until a socket frees
        static uint32_t last_try = 0;
        uint32_t now = millis();
        if (now - last_try < 500) return;
        last_try = now;
        s_bound = (udp.begin(BACNET_PORT) != 0);
        if (!s_bound) return;
        diagnostic_log_printf(diagnostics::Origin::Application,"[BACnet] acquired UDP 47808 socket");
    }
    int sz = udp.parsePacket();
    if (sz <= 0) return;
    uint8_t buf[128];
    int n = udp.read(buf, sizeof(buf));
    if (n < 6 || buf[0] != 0x81) return;           // BVLC type 0x81 = BACnet/IP
    // NPDU starts at offset 4: version, control
    int i = 4;
    if (buf[i] != 0x01) return;                    // NPDU version 1
    uint8_t control = buf[i + 1];
    i += 2;
    if (control & 0x20) { if (i + 3 > n) return; uint8_t dlen = buf[i + 2]; i += 3 + dlen; }  // dest
    if (control & 0x08) { if (i + 3 > n) return; uint8_t slen = buf[i + 2]; i += 3 + slen; }  // source
    if (control & 0x20) { if (i < n) i += 1; }     // hop count (present only with destination)
    if (i + 2 > n) return;

    // APDU: Who-Is = Unconfirmed-Request (0x10), service choice 0x08.
    if (buf[i] == 0x10 && buf[i + 1] == 0x08) {
        udp.remoteIP().toString().toCharArray(cur_src_ip, sizeof(cur_src_ip));
        uint8_t apdu[32];
        int al = build_iam(apdu);
        uint8_t out[64]; uint8_t* o = out;
        int total = 4 + 2 + al;                    // BVLC(4) + NPDU(2) + APDU
        *o++ = 0x81; *o++ = 0x0A;                  // BVLC: BACnet/IP, Original-Unicast-NPDU
        *o++ = (total >> 8) & 0xFF; *o++ = total & 0xFF;
        *o++ = 0x01; *o++ = 0x00;                  // NPDU version 1, control 0 (no dest/src)
        memcpy(o, apdu, al); o += al;
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(out, (size_t)(o - out));
        udp.endPacket();
        plc_log(SRC_BACNET, cur_src_ip, "BACnet Who-Is -> I-Am (device %lu enumeration)",
                (unsigned long)BACNET_DEVICE_INSTANCE);
    }
}
#else
bool BacnetResponder::start() { return false; }
void BacnetResponder::handle() {}
#endif
