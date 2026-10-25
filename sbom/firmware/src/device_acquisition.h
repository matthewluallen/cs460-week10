#pragma once
#include "device_acquisition_core.h"
acquisition::Engine& device_acquisition_state();
void device_acquisition_bind(const devices::Inventory& inventory);
void device_acquisition_tick();
bool device_acquisition_pause();
