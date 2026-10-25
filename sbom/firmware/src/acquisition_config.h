#pragma once
#include <stdint.h>
namespace acquisition {
// One source of truth for the fixed-heater raw pilot. Changing this contract
// requires a new identifier, pinned app and physical/same-app acceptance.
constexpr uint16_t EnvHeaterC=320, EnvHeaterMs=150;
constexpr const char* EnvConfigId="bme68x-forced-320c-150ms-v1";
}
