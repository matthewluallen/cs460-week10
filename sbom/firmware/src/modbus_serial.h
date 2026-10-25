// modbus_serial.h - "Modbus/TCP over USB serial" gateway mode.
//
// The board's USB CDC serial normally carries the text `plc>` console. In gateway
// mode it instead carries raw Modbus/TCP (MBAP-framed) request/response frames, so
// the student's LOCAL board can be reached from the cloud lab environment with NO
// laptop install and NO tunnel: a browser (Web Serial) pipes the serial bytes over
// a WebSocket to a small cloud gateway that presents them as Modbus/TCP.
//
//   Codespace tools --TCP--> cloud gateway --WS--> browser --USB--> [this mode]
//
// Entering gateway mode SILENCES all text telemetry on the USB serial (the stream
// must be pure Modbus). Exit by resetting the board. The same coil/register image
// is shared with the Ethernet Modbus path and the scan (spinlock-guarded), so the
// USB gateway is just another unauthenticated Modbus front door (CWE-306) - which
// is itself part of the lesson.
#pragma once
#include <Arduino.h>
void modbus_serial_enter();    // switch the USB serial into Modbus-gateway mode
bool modbus_serial_active();   // true while in gateway mode (text output suppressed)
void modbus_serial_poll();     // pump serial<->Modbus while active (call from loop)
