#pragma once
#include <Arduino.h>

// One owner per boot. A saved change takes effect only after reboot.
// Hall = an M5Stack digital hall-effect unit (A3144E) on the BLUE port's SIG
// pin — a GPIO switch, not an I2C device, so it never appears in the Port A
// (Wire1) inventory scan and must be selected here explicitly.
enum class PortCProfile : uint8_t { Disabled = 0, HX711 = 1, H2Uart = 2, Hall = 3 };
void port_c_begin();
PortCProfile port_c_profile();
void port_c_console(const char* operation, const char* argument, bool extra);
// Poll the hall switch (no-op unless the Hall profile is active). Cheap; call
// from the main loop. Tracks the current magnet state and counts activations.
void port_c_poll();
// Current debounced hall state (false unless Hall profile active and a magnet
// is present). Open-collector A3144E pulls the SIG line LOW when it latches.
bool port_c_hall_present();
