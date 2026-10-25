#ifndef IEC104_OUTSTATION_H
#define IEC104_OUTSTATION_H
#include <Arduino.h>
#include "net_types.h"
#include "config.h"

// IEC 60870-5-104 outstation — deliberately-vulnerable teaching target (LS-19).
// The European/utility counterpart to the DNP3 outstation: connection-oriented
// (TCP 2404) with APCI I/S/U frames and N(S)/N(R) sequencing. Answers STARTDT,
// TESTFR, and a General Interrogation (C_IC_NA_1) from the shared StamPLC image
// (single points = DIs + relays, measured values = analog), and accepts an
// unauthenticated single command (C_SC_NA_1) that actuates a relay via the same
// path as Modbus/DNP3 — no base-protocol authentication (CWE-306).
class Iec104Outstation {
public:
    Iec104Outstation();
    bool start();
    void handle();
private:
    NetServer server;
    uint16_t s_vs, s_vr;          // send / receive sequence numbers
    bool     s_started;           // STARTDT active
    char     cur_src_ip[24];
    void send_u(NetClient& c, uint8_t cmd);
    void send_i(NetClient& c, const uint8_t* asdu, int len);
    int  build_sp(uint8_t* a, uint8_t cot);        // M_SP_NA_1: DIs (IOA 1-8) + relays (11-14)
    int  build_me(uint8_t* a, uint8_t cot);        // M_ME_NB_1: analog (IOA 21-24)
    int  build_ic(uint8_t* a, uint8_t cot);        // C_IC_NA_1 actcon / actterm
};
extern Iec104Outstation iec104_outstation;
#endif // IEC104_OUTSTATION_H
