#ifndef DNP3_OUTSTATION_H
#define DNP3_OUTSTATION_H
#include <Arduino.h>
#include "net_types.h"
#include "config.h"
#include "dnp3_wire.h"

// DNP3 (IEEE 1815) outstation — deliberately-vulnerable teaching target (LS-19).
// The DNP3 counterpart of the on-board Modbus/TCP target: no base-protocol
// authentication, so a CROB DIRECT_OPERATE actuates a relay with no credential
// check — the DNP3 analog of the Modbus CWE-306 unauth coil write. Secure
// Authentication (SAv5) is the defense the course teaches. A Class-0 poll is
// answered from the shared StamPLC I/O image (DIs, relays, analog registers) via
// the same coil/register path Modbus uses, so both protocols see one process.
class Dnp3Outstation {
public:
    Dnp3Outstation();
    bool start();
    void handle();                 // call periodically from the comms task / loop
private:
    NetServer server;
    NetClient active;
    dnp3::Stream stream;
    dnp3::Transport transport;
    uint64_t last_rx_ms = 0;
    uint8_t response_sequence = 0;
    bool link_seen = false, link_fcb = false;
    int  build_response(uint8_t* out, uint16_t dest);
    int  build_control_reply(uint8_t* out, const uint8_t* obj, int objlen, uint16_t dest);
    bool apply_crob(const uint8_t* obj, int len, const char* src_ip, uint32_t peer);
    char cur_src_ip[24];
};
extern Dnp3Outstation dnp3_outstation;
#endif // DNP3_OUTSTATION_H
