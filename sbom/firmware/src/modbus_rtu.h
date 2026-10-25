// modbus_rtu.h — Modbus RTU slave on the StamPLC RS-485 bus (LS-18).
//
// A real RTU sibling of the Modbus/TCP server, bound to the SAME I/O image
// (it reuses ModbusTCPServer::handle_frame, so coils/registers, the function-code
// handlers, and the intentional vulnerabilities are shared) — "the map is the
// security model." Runs on UART_NUM_1 in RS-485 half-duplex (the board's A/B
// terminals); any node on the multidrop bus can master it (no auth — the lesson).
// Enabled via the service catalog (SVC_MODBUS_RTU); gated in main.cpp.
#pragma once
#include <Arduino.h>

void modbus_rtu_begin(uint8_t slave_id, uint32_t baud);  // start the sole UART/DIR owner
bool modbus_rtu_active();
void modbus_rtu_test_tx();  // queue a known reverse-link test; owner transmits it
void modbus_rtu_diag();     // read-only counters; USB framing limits still apply
