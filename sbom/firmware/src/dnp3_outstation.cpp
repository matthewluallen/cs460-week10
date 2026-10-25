#include "dnp3_outstation.h"
#include "modbus_tcp_server.h"   // shared I/O image: get_register / set_coil (same path as Modbus)
#include "plc_core.h"            // plc_di_live / plc_coil_live / plc_log / SRC_MODBUS
#include <string.h>
#include "esp_timer.h"

Dnp3Outstation dnp3_outstation;

// The same allocation-free framing/reassembly code is tested natively and runs
// on the physical W5500 and its QEMU model.
static int dnp3_build_frame(uint8_t* out, uint16_t dest, uint16_t src, uint8_t ctrl,
                            const uint8_t* app, int applen) {
    if (applen < 0) return 0;
    return int(dnp3::encode(out, 300, dest, src, ctrl, app, size_t(applen)));
}

Dnp3Outstation::Dnp3Outstation() : server(DNP3_TCP_PORT) { cur_src_ip[0] = 0; }

bool Dnp3Outstation::start() {
    for (int i = 0; i < 3; i++) { server.begin(); delay(20); }  // 3x = boot retry robustness (begin() is best-effort); available() maintains one listener thereafter
    Serial.printf("[DNP3] outstation (addr %d) started on TCP %d - no base auth (SAv5 is the defense)\n",
                  DNP3_ADDR, DNP3_TCP_PORT);
    return true;
}

// Build a Class-0 response: g1v2 binary inputs (8 DIs), g10v2 binary output status
// (4 relays), g30v2 16-bit analog inputs (4 pts) — all from the live I/O image.
int Dnp3Outstation::build_response(uint8_t* out, uint16_t dest) {
    uint8_t app[160]; int a = 0;
    app[a++] = 0xC0;                          // transport: FIR|FIN, seq 0
    app[a++] = uint8_t(0xC0 | response_sequence); // echo request application sequence
    app[a++] = 0x81;                          // function: RESPONSE
    app[a++] = 0x00; app[a++] = 0x00;         // IIN (no internal indications)

    // g1v2 binary input with flags: online(0x01) + state(0x80)
    app[a++] = 0x01; app[a++] = 0x02; app[a++] = 0x00; app[a++] = 0x00; app[a++] = 0x07;
    for (int i = 0; i < 8; i++) app[a++] = plc_di_live(i) ? 0x81 : 0x01;

    // g10v2 binary output status (relays QX0-3)
    app[a++] = 0x0A; app[a++] = 0x02; app[a++] = 0x00; app[a++] = 0x00; app[a++] = 0x03;
    for (int i = 0; i < 4; i++) app[a++] = plc_coil_live(i) ? 0x81 : 0x01;

    // g30v2 16-bit analog input with flag: Vin(mV), current(mA), CH1, CH2
    app[a++] = 0x1E; app[a++] = 0x02; app[a++] = 0x00; app[a++] = 0x00; app[a++] = 0x03;
    const uint16_t regs[4] = { modbus_server.get_register(0),  modbus_server.get_register(1),
                               modbus_server.get_register(10), modbus_server.get_register(12) };
    for (int i = 0; i < 4; i++) { app[a++] = 0x01; app[a++] = regs[i] & 0xFF; app[a++] = regs[i] >> 8; }

    return dnp3_build_frame(out, dest, DNP3_ADDR, 0x44, app, a);
}

// Echo the control object back with a RESPONSE (status unchanged = success).
int Dnp3Outstation::build_control_reply(uint8_t* out, const uint8_t* obj, int objlen, uint16_t dest) {
    uint8_t app[160]; int a = 0;
    app[a++] = 0xC0; app[a++] = uint8_t(0xC0 | response_sequence); app[a++] = 0x81; app[a++] = 0x00; app[a++] = 0x00;
    int cp = objlen; if (cp > (int)sizeof(app) - a) cp = sizeof(app) - a;
    memcpy(app + a, obj, cp); a += cp;
    return dnp3_build_frame(out, dest, DNP3_ADDR, 0x44, app, a);
}

// Apply a CROB (g12v1) DIRECT_OPERATE: actuate the target relay through the SAME
// coil path as Modbus, with NO authentication (CWE-306) — the teaching point.
void Dnp3Outstation::apply_crob(const uint8_t* obj, int len, const char* src_ip) {
    if (len < 5 || obj[0] != 0x0C || obj[1] != 0x01) return;   // expect g12v1 CROB
    uint8_t count = obj[3];                                    // qualifier 0x17: 1-byte count + index prefix
    int p = 4;
    for (int k = 0; k < count && p + 12 <= len; k++) {
        uint8_t index = obj[p];
        uint8_t code  = obj[p + 1];                            // CROB control code
        bool on;
        if      (code == 0x03 || code == 0x41) on = true;      // LATCH_ON / PULSE_ON (close)
        else if (code == 0x04 || code == 0x81) on = false;     // LATCH_OFF / PULSE_OFF (trip)
        else                                    on = true;
        if (index < 4) {
            modbus_server.set_coil(index, on);                 // same actuation path as Modbus; no auth
            plc_log(SRC_DNP3, src_ip, "DNP3 DIRECT_OPERATE relay %u = %d (CROB 0x%02X)", index, on ? 1 : 0, code);
        }
        p += 12;                                               // index(1)+code(1)+opcount(1)+on(4)+off(4)+status(1)
    }
}

void Dnp3Outstation::handle() {
    const uint64_t now = uint64_t(esp_timer_get_time()) / 1000;
    if (active && (!active.connected() || (stream.buffered() && now - last_rx_ms > 2000))) {
        active.stop(); stream.clear(); transport.clear(); link_seen = false;
    }
    NetClient candidate = server.available();
    if (candidate && (!active || candidate != active)) {
        // One persistent master connection per declared embedded profile.
        if (active && active.connected()) { candidate.stop(); return; }
        active = candidate; stream.clear(); transport.clear(); link_seen = false;
    }
    if (!active || !active.connected()) return;
    // Bounded work per comms iteration; a split TCP frame remains buffered.
    for (unsigned budget = 0; budget < 512 && active.available(); ++budget) {
        int byte = active.read(); if (byte < 0) break;
        last_rx_ms = now;
        dnp3::LinkFrame frame;
        if (!stream.push(uint8_t(byte), frame)) continue;
        // Reserved/broadcast destinations and non-master direction are outside
        // this profile and cannot invoke application processing.
        if (frame.destination != DNP3_ADDR || frame.source >= 0xfff0 ||
            (frame.control & 0xc0) != 0xc0) continue;
        uint8_t function = frame.control & 15;
        uint8_t out[300]; int size = 0;
        if (function == 0 && !(frame.control & 0x10) && !frame.length) {
            transport.clear(); link_seen = false;
            size = dnp3_build_frame(out, frame.source, DNP3_ADDR, 0, nullptr, 0);
        } else if (function == 9 && !(frame.control & 0x10) && !frame.length) {
            size = dnp3_build_frame(out, frame.source, DNP3_ADDR, 11, nullptr, 0);
        } else if ((function == 4 && !(frame.control & 0x30)) || (function == 3 && (frame.control & 0x10))) {
            if (function == 3) {
                size = dnp3_build_frame(out, frame.source, DNP3_ADDR, 0, nullptr, 0);
                if (active.write(out, size) != size_t(size)) { active.stop(); break; }
                size = 0;
                const bool fcb = frame.control & 0x20;
                if (link_seen && fcb == link_fcb) continue;
                link_seen = true; link_fcb = fcb;
            }
            if (transport.accept(frame, now) != dnp3::Transport::Complete) continue;
            const uint8_t* app = transport.data(); const size_t n = transport.size();
            if (n < 2 || (app[0] & 0xf0) != 0xc0) continue;
            response_sequence = app[0] & 15;
            active.remoteIP().toString().toCharArray(cur_src_ip, sizeof(cur_src_ip));
            if (app[1] == 1) {
                plc_log(SRC_DNP3, cur_src_ip, "DNP3 READ from addr %u", frame.source);
                size = build_response(out, frame.source);
            } else if (app[1] == 5 || app[1] == 6) {
                apply_crob(app + 2, int(n - 2), cur_src_ip);
                // Function 6 explicitly forbids an application response.
                size = app[1] == 6 ? 0 : build_control_reply(out, app + 2, int(n - 2), frame.source);
            } else if (app[1] != 0) {
                const uint8_t error[] = {0xc0, uint8_t(0xc0 | response_sequence), 0x81, 0, 1};
                size = dnp3_build_frame(out, frame.source, DNP3_ADDR, 0x44, error, sizeof(error));
            }
        }
        if (size && active.write(out, size) != size_t(size)) { active.stop(); break; }
    }
}
