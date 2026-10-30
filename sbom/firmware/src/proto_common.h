#ifndef PROTO_COMMON_H
#define PROTO_COMMON_H
#include <Arduino.h>
// Shared helpers for the discovery/outstation protocol modules — these blocks were
// copy-pasted into each of dnp3/iec104/opcua/enip/s7.
//
// Best-effort listener bring-up: begin() is retried a few times at boot (it is
// best-effort on the W5500), and available() maintains a single listener thereafter.
template <class Server> inline void net_listen_begin(Server& s) {
    for (int i = 0; i < 3; i++) { s.begin(); delay(20); }
}
// The "legacy fixed" 4-analog snapshot DNP3 and IEC-104 both publish: VIN_MV,
// CURRENT_MA, AIO_CH1_MV, AIO_CH2_MV from the Modbus holding image.
void plc_legacy_analog_snapshot(uint16_t out[4]);
#endif
