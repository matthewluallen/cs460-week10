// plc_console.h - live USB debug terminal. Non-blocking line reader on the USB-C
// serial port with a real PLC command set: status, points, show, set, force,
// mode, faults, log, mon. Value-setting over USB is unauthenticated by design
// (physical-port trust) and every change is written to the source-attributed
// audit log as source=USB. PLCLOAD frames are handed to the program loader.
#pragma once
void plc_console_begin();
void plc_console_poll();          // call every loop()
bool console_telemetry_muted();  // true while an operator is typing or 'quiet on'
