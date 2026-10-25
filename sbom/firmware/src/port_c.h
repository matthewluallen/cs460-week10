#pragma once
#include <Arduino.h>

// One owner per boot. A saved change takes effect only after reboot.
enum class PortCProfile : uint8_t { Disabled = 0, HX711 = 1, H2Uart = 2 };
void port_c_begin();
PortCProfile port_c_profile();
void port_c_console(const char* operation, const char* argument, bool extra);
