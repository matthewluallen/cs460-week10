#include "s7_fingerprint.h"
#include "plc_core.h"          // plc_log / SRC_MODBUS
#include <string.h>

S7Fingerprint s7_fingerprint;

// ---- Siemens CPU 315-2 PN/DP identity (isolated lab honeypot; matches Snap7/Conpot).
static const char    S7_ORDER_NO[]   = "6ES7 315-2EH14-0AB0";   // MLFB order number
static const uint8_t S7_FW[3]        = { 3, 2, 6 };             // firmware V3.2.6
static const char    S7_SYSTEM_NAME[] = "SIMATIC 300(1)";
static const char    S7_MODULE_NAME[] = "CPU 315-2 PN/DP";
static const char    S7_PLANT_ID[]    = "TBD-LAB";
static const char    S7_COPYRIGHT[]   = "Original Siemens Equipment";
static const char    S7_SERIAL[]      = "S C-C2UR28922012";

S7Fingerprint::S7Fingerprint() : server(S7_TCP_PORT) { cur_src_ip[0] = 0; }

bool S7Fingerprint::start() {
    for (int i = 0; i < 3; i++) { server.begin(); delay(20); }  // 3x = boot retry robustness (begin() is best-effort); available() maintains one listener thereafter
    Serial.printf("[S7] S7comm/ISO-TSAP responder on TCP %d - CPU 315-2 PN/DP (lab honeypot, unauth)\n", S7_TCP_PORT);
    return true;
}

static int tpkt(uint8_t* out, const uint8_t* payload, int plen) {
    int total = 4 + plen;
    out[0] = 0x03; out[1] = 0x00; out[2] = (total >> 8) & 0xFF; out[3] = total & 0xFF;
    memcpy(out + 4, payload, plen);
    return total;
}

static void put_field(uint8_t* b, const char* s, int n) {
    int len = strlen(s); if (len > n) len = n;
    memcpy(b, s, len);
    for (int i = len; i < n; i++) b[i] = 0x00;             // null-pad (nmap reads null-terminated)
}

// Build an S7 userdata ReadSZL response for SZL-ID 0x0011 or 0x001C, laid out so
// each identity string lands at the exact byte offset nmap s7-info parses.
int S7Fingerprint::build_szl(uint8_t* out, uint16_t seq, uint16_t szl_id) {
    static const uint8_t pre[] = {
        0x03,0x00,0x00,0x00,                                   // TPKT (len backfilled)
        0x02,0xF0,0x80,                                        // COTP DT
        0x32,0x07,0x00,0x00,0x00,0x00,                         // S7 userdata hdr (pduref @11-12)
        0x00,0x0C,0x00,0x00,                                   // ParLen=0x000C, DataLen @15-16
        0x00,0x01,0x12,0x08,0x12,0x84,0x01,0x01,0x00,0x00,0x00,0x00,  // response param (12)
        0xFF,0x09,0x00,0x00                                    // retcode, transport, len16 @31-32
    };
    memcpy(out, pre, sizeof(pre));
    out[11] = seq >> 8; out[12] = seq & 0xFF;
    int p = sizeof(pre);                                       // 33: SZL header start
    uint8_t reclen  = (szl_id == 0x0011) ? 28 : 34;
    uint8_t reccount = (szl_id == 0x0011) ? 3 : 5;
    out[p++] = szl_id >> 8; out[p++] = szl_id & 0xFF;
    out[p++] = 0x00; out[p++] = 0x01;                         // index
    out[p++] = 0x00; out[p++] = reclen;
    out[p++] = 0x00; out[p++] = reccount;                     // records start at byte 42 (1-based)
    if (szl_id == 0x0011) {
        const uint16_t idx[3] = { 0x0001, 0x0006, 0x0007 };
        for (int r = 0; r < 3; r++) {
            uint8_t* b = out + p; memset(b, 0, 28);
            b[0] = idx[r] >> 8; b[1] = idx[r] & 0xFF;
            put_field(b + 2, S7_ORDER_NO, 20);                // MLFB @ record+2 -> byte 44/72/100
            if (idx[r] == 0x0007) { b[25] = S7_FW[0]; b[26] = S7_FW[1]; b[27] = S7_FW[2]; }  // version @123
            p += 28;
        }
    } else {                                                   // 0x001C
        struct { uint16_t idx; const char* val; } rec[5] = {
            {1, S7_SYSTEM_NAME}, {2, S7_MODULE_NAME}, {3, S7_PLANT_ID},
            {4, S7_COPYRIGHT},   {5, S7_SERIAL} };
        for (int r = 0; r < 5; r++) {
            uint8_t* b = out + p; memset(b, 0, 34);
            b[0] = rec[r].idx >> 8; b[1] = rec[r].idx & 0xFF;
            put_field(b + 2, rec[r].val, 32);                 // value @ record+2 -> 44/78/112/146/180
            p += 34;
        }
    }
    int szl_block = p - 33;
    out[31] = (szl_block >> 8) & 0xFF; out[32] = szl_block & 0xFF;   // transport length
    int datalen = 4 + szl_block;
    out[15] = (datalen >> 8) & 0xFF;  out[16] = datalen & 0xFF;      // S7 DataLen
    out[2]  = (p >> 8) & 0xFF;        out[3]  = p & 0xFF;            // TPKT length
    return p;
}

void S7Fingerprint::handle() {
    NetClient client = server.available();
    if (!client) return;
    if (!client.connected()) { client.stop(); return; }
    if (!client.available()) return;

    uint8_t req[300];
    size_t want = client.available(); if (want > sizeof(req)) want = sizeof(req);
    int len = client.readBytes(req, want);
    if (len < 6 || req[0] != 0x03 || req[1] != 0x00) return;       // not TPKT

    client.remoteIP().toString().toCharArray(cur_src_ip, sizeof(cur_src_ip));
    uint8_t cotp_type = req[5];
    uint8_t out[300]; int ol = 0;

    if (cotp_type == 0xE0) {
        // COTP Connection Request -> Connection Confirm.
        uint8_t co[64]; int c = 0;
        int param_off = 11, param_len = (int)req[4] - 6;
        if (param_len < 0) param_len = 0;
        if (param_off + param_len > len) param_len = len - param_off;
        if (param_len < 0) param_len = 0;
        co[c++] = 0;                                                // LI placeholder
        co[c++] = 0xD0;                                            // Connection Confirm
        co[c++] = req[8]; co[c++] = req[9];                        // dst_ref = client's src_ref
        co[c++] = 0x00;   co[c++] = 0x01;                          // our src_ref
        co[c++] = (req[10] & 0x0F);                                // class/option
        for (int i = 0; i < param_len && c < (int)sizeof(co); i++) co[c++] = req[param_off + i];
        co[0] = (uint8_t)(c - 1);                                  // LI
        ol = tpkt(out, co, c);
        plc_log(SRC_S7, cur_src_ip, "S7/ISO-TSAP COTP connect (device fingerprint)");
    } else if (cotp_type == 0xF0 && len >= 9 && req[7] == 0x32) {
        const int s7 = 7;
        uint8_t  rosctr = req[s7 + 1];
        uint16_t seq    = ((uint16_t)req[s7 + 4] << 8) | req[s7 + 5];
        uint16_t parlen = ((uint16_t)req[s7 + 6] << 8) | req[s7 + 7];
        if (rosctr == 0x01 && len > s7 + 10 && req[s7 + 10] == 0xF0) {
            // S7 Communication Setup -> ack. Echo the client's parallel-job counts
            // and PDU length (Snap7 fidelity), defaulting to 240 if absent.
            const uint8_t* rp = req + s7 + 10;                     // request setup param
            uint8_t amq_c1_hi = (len > s7 + 13) ? rp[2] : 0x00, amq_c1_lo = (len > s7 + 13) ? rp[3] : 0x01;
            uint8_t amq_c2_hi = (len > s7 + 15) ? rp[4] : 0x00, amq_c2_lo = (len > s7 + 15) ? rp[5] : 0x01;
            uint8_t pdu_hi    = (len > s7 + 17) ? rp[6] : 0x00, pdu_lo    = (len > s7 + 17) ? rp[7] : 0xF0;
            uint8_t body[] = {
                0x02, 0xF0, 0x80,                                  // COTP DT
                0x32, 0x03, 0x00, 0x00,                            // S7 ack_data
                (uint8_t)(seq >> 8), (uint8_t)(seq & 0xFF),
                0x00, 0x08, 0x00, 0x00,                            // ParLen 8, DataLen 0
                0x00, 0x00,                                        // error class/code
                0xF0, 0x00, amq_c1_hi, amq_c1_lo, amq_c2_hi, amq_c2_lo, pdu_hi, pdu_lo
            };
            ol = tpkt(out, body, sizeof(body));
            plc_log(SRC_S7, cur_src_ip, "S7 Setup Communication - S7comm fingerprint");
        } else if (rosctr == 0x07) {
            // S7 userdata: ReadSZL. Locate the SZL-ID in the data section.
            int ds = s7 + 10 + parlen;                             // data starts after the param
            if (ds + 6 <= len) {
                uint16_t szl_id = ((uint16_t)req[ds + 4] << 8) | req[ds + 5];
                if (szl_id == 0x0011 || szl_id == 0x001C) {
                    ol = build_szl(out, seq, szl_id);
                    plc_log(SRC_S7, cur_src_ip, "S7 ReadSZL 0x%04X (module/component identification)", szl_id);
                }
            }
        }
    }
    if (ol > 0) client.write(out, ol);
}
