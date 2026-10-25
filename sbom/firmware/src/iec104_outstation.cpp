#include "iec104_outstation.h"
#include "modbus_tcp_server.h"   // shared I/O image: get_register / set_coil (same path as Modbus)
#include "plc_core.h"            // plc_di_live / plc_coil_live / plc_log / SRC_MODBUS
#include <string.h>

Iec104Outstation iec104_outstation;

#define IEC104_CA 1              // common address of ASDU

Iec104Outstation::Iec104Outstation() : server(IEC104_TCP_PORT) {
    s_vs = 0; s_vr = 0; s_started = false; cur_src_ip[0] = 0;
}

bool Iec104Outstation::start() {
    for (int i = 0; i < 3; i++) { server.begin(); delay(20); }  // 3x = boot retry robustness (begin() is best-effort); available() maintains one listener thereafter
    Serial.printf("[IEC104] IEC 60870-5-104 outstation on TCP %d (CA %d) - no base auth\n",
                  IEC104_TCP_PORT, IEC104_CA);
    return true;
}

// U-format (unnumbered control): STARTDT/STOPDT/TESTFR act/con.
void Iec104Outstation::send_u(NetClient& c, uint8_t cmd) {
    uint8_t f[6] = { 0x68, 0x04, cmd, 0x00, 0x00, 0x00 };
    c.write(f, 6);
}

// I-format (information transfer): APCI with N(S)/N(R) + ASDU. Bumps N(S).
void Iec104Outstation::send_i(NetClient& c, const uint8_t* asdu, int len) {
    uint8_t f[260]; int n = 0;
    f[n++] = 0x68;
    f[n++] = (uint8_t)(4 + len);
    f[n++] = (uint8_t)((s_vs & 0x7F) << 1);      // ctrl1: N(S) low, bit0=0 (I-format)
    f[n++] = (uint8_t)((s_vs >> 7) & 0xFF);      // ctrl2: N(S) high
    f[n++] = (uint8_t)((s_vr & 0x7F) << 1);      // ctrl3: N(R) low
    f[n++] = (uint8_t)((s_vr >> 7) & 0xFF);      // ctrl4: N(R) high
    memcpy(f + n, asdu, len); n += len;
    c.write(f, n);
    s_vs++;
}

// ASDU header helper written inline in each builder: TypeID, VSQ, COT, OA, CA(2).

int Iec104Outstation::build_sp(uint8_t* a, uint8_t cot) {
    int n = 0;
    a[n++] = 1;                                  // M_SP_NA_1 (single-point information)
    a[n++] = 12;                                 // VSQ: 12 objects, SQ=0 (individual IOAs)
    a[n++] = cot; a[n++] = 0;                    // COT, originator address
    a[n++] = IEC104_CA & 0xFF; a[n++] = (IEC104_CA >> 8) & 0xFF;
    for (int i = 0; i < 8; i++) {                // DIs at IOA 1..8
        uint32_t ioa = 1 + i;
        a[n++] = ioa & 0xFF; a[n++] = (ioa >> 8) & 0xFF; a[n++] = (ioa >> 16) & 0xFF;
        a[n++] = plc_di_live(i) ? 0x01 : 0x00;   // SIQ: SPI + good quality
    }
    for (int i = 0; i < 4; i++) {                // relays at IOA 11..14
        uint32_t ioa = 11 + i;
        a[n++] = ioa & 0xFF; a[n++] = (ioa >> 8) & 0xFF; a[n++] = (ioa >> 16) & 0xFF;
        a[n++] = plc_coil_live(i) ? 0x01 : 0x00;
    }
    return n;
}

int Iec104Outstation::build_me(uint8_t* a, uint8_t cot) {
    int n = 0;
    a[n++] = 11;                                 // M_ME_NB_1 (measured value, scaled)
    a[n++] = 4;                                  // 4 objects
    a[n++] = cot; a[n++] = 0;
    a[n++] = IEC104_CA & 0xFF; a[n++] = (IEC104_CA >> 8) & 0xFF;
    uint16_t regs[4] = { modbus_server.get_register(0),  modbus_server.get_register(1),
                         modbus_server.get_register(10), modbus_server.get_register(12) };
    for (int i = 0; i < 4; i++) {                // analog at IOA 21..24
        uint32_t ioa = 21 + i;
        a[n++] = ioa & 0xFF; a[n++] = (ioa >> 8) & 0xFF; a[n++] = (ioa >> 16) & 0xFF;
        int16_t v = (int16_t)regs[i];
        a[n++] = v & 0xFF; a[n++] = (v >> 8) & 0xFF;   // scaled value (LE, signed)
        a[n++] = 0x00;                                 // QDS: good quality
    }
    return n;
}

int Iec104Outstation::build_ic(uint8_t* a, uint8_t cot) {
    int n = 0;
    a[n++] = 100;                                // C_IC_NA_1 (interrogation command)
    a[n++] = 1; a[n++] = cot; a[n++] = 0;
    a[n++] = IEC104_CA & 0xFF; a[n++] = (IEC104_CA >> 8) & 0xFF;
    a[n++] = 0; a[n++] = 0; a[n++] = 0;          // IOA = 0
    a[n++] = 0x14;                               // QOI = 20 (station interrogation, global)
    return n;
}

void Iec104Outstation::handle() {
    NetClient client = server.available();
    if (!client) return;
    if (!client.connected()) { client.stop(); return; }
    if (!client.available()) return;

    uint8_t buf[512];
    size_t want = client.available(); if (want > sizeof(buf)) want = sizeof(buf);
    int len = client.readBytes(buf, want);
    client.remoteIP().toString().toCharArray(cur_src_ip, sizeof(cur_src_ip));

    int i = 0;
    while (i + 2 <= len && buf[i] == 0x68) {
        int alen = buf[i + 1];
        if (i + 2 + alen > len) break;                       // incomplete APDU
        const uint8_t* apci = buf + i + 2;                    // 4 control octets
        uint8_t c1 = apci[0];

        if ((c1 & 0x03) == 0x03) {                            // U-format
            if      (c1 == 0x07) { s_started = true; s_vs = 0; s_vr = 0; send_u(client, 0x0B);
                                   plc_log(SRC_IEC104, cur_src_ip, "IEC-104 STARTDT (session start)"); }
            else if (c1 == 0x13) { s_started = false; send_u(client, 0x23); }   // STOPDT -> con
            else if (c1 == 0x43) { send_u(client, 0x83); }                      // TESTFR -> con
        } else if ((c1 & 0x01) == 0x00) {                     // I-format
            s_vr++;                                           // count the received I-frame
            const uint8_t* asdu = apci + 4;
            int asdulen = alen - 4;
            if (asdulen >= 6) {
                uint8_t type = asdu[0];
                if (type == 100) {                            // C_IC_NA_1: General Interrogation
                    plc_log(SRC_IEC104, cur_src_ip, "IEC-104 General Interrogation (C_IC_NA_1)");
                    uint8_t a[128]; int l;
                    l = build_ic(a, 0x07); send_i(client, a, l);   // actcon (COT 7)
                    l = build_sp(a, 0x14); send_i(client, a, l);   // single points (COT 20)
                    l = build_me(a, 0x14); send_i(client, a, l);   // measured values (COT 20)
                    l = build_ic(a, 0x0A); send_i(client, a, l);   // actterm (COT 10)
                } else if (type == 45) {                      // C_SC_NA_1: single command
                    uint32_t ioa = (uint32_t)asdu[6] | ((uint32_t)asdu[7] << 8) | ((uint32_t)asdu[8] << 16);
                    uint8_t sco = asdu[9];
                    bool on = sco & 0x01;                     // SCS: command state
                    int relay = (int)ioa - 11;
                    if (relay >= 0 && relay < 4) {
                        modbus_server.set_coil(relay, on);    // same actuation path as Modbus; no auth
                        plc_log(SRC_IEC104, cur_src_ip, "IEC-104 C_SC_NA_1 relay %d = %d (IOA %u, unauth)", relay, on ? 1 : 0, ioa);
                    }
                    uint8_t a[16]; int n = 0;                 // actcon: echo the command, COT 7
                    a[n++] = 45; a[n++] = 1; a[n++] = 0x07; a[n++] = 0;
                    a[n++] = IEC104_CA & 0xFF; a[n++] = (IEC104_CA >> 8) & 0xFF;
                    a[n++] = asdu[6]; a[n++] = asdu[7]; a[n++] = asdu[8]; a[n++] = sco;
                    send_i(client, a, n);
                }
            }
        }
        // S-format (c1 & 0x03 == 0x01): acknowledgement only, nothing to send.
        i += 2 + alen;
    }
}
