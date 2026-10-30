// io_expansion.h - StamPLC IO expansion module (analog inputs + relays).
// Thin wrapper around the vendored M5Stack M5StamPLC_IO driver (MIT) so the
// Modbus layer and main loop stay free of the module's class internals.
#pragma once
#include <Arduino.h>

// Scan/init the IO expansion module on the internal I2C bus (0x20-0x2F).
// Returns true if a module was found. Safe to call once in setup().
bool ioexp_begin();

// True if a module was found by ioexp_begin().
bool ioexp_present();

// Read the two analog channels. v in mV, i in uA. No-op returning false if
// the module is absent.
bool ioexp_read(int16_t* v1_mV, int32_t* i1_uA, int16_t* v2_mV, int32_t* i2_uA);

// Drive the module's mechanical safety relay (N.C/COM/N.O). No-op if absent.
void ioexp_set_mech_relay(bool on);

// Drive an IO-expansion solid-state relay: idx 0 = SSR1, idx 1 = SSR2.
void ioexp_set_ssr(uint8_t idx, bool on);

// Control-register readback, not electrical/contact feedback. Returns false
// on an absent module or failed I2C read. Bits 0/1=SSR, bit4=mechanical relay.
bool ioexp_output_readback(uint8_t* bits);
// Loop-owner identity/status observation, not a unique device serial or contact proof.
bool ioexp_identity(uint8_t* address,uint8_t* revision);

uint8_t ioexp_addr();  // current I2C address, 0 if absent
uint8_t ioexp_fw();    // module firmware version, 0 if absent
