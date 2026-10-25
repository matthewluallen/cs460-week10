#ifndef BACNET_RESPONDER_H
#define BACNET_RESPONDER_H
#include <Arduino.h>
#include "config.h"

// BACnet/IP device responder - building-automation enumeration target.
// Answers the Who-Is broadcast that every BACnet discovery tool (YABE, the
// BACnet Discovery Tool, nmap bacnet-info, Wireshark) uses to find devices on
// UDP 47808, replying with I-Am (device instance / max-APDU / segmentation /
// vendor id). BACnet has no authentication by design, so this over-discloses a
// device identity to anyone on the segment - the enumeration / information-
// disclosure lesson, matching the S7 and EtherNet/IP responders. Teaching
// identity only (a teaching device instance + a non-registered vendor id).
class BacnetResponder {
public:
    BacnetResponder();
    bool start();
    void handle();   // call every comms loop
private:
    char cur_src_ip[24];
};
extern BacnetResponder bacnet_responder;
#endif // BACNET_RESPONDER_H
