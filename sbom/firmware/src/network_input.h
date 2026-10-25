#pragma once
#include "network_input_core.h"
// Config/published state are guarded by the PLC image lock. No I/O in these calls.
void network_input_configure(unsigned slot,bool enabled,const network_input::Config& config);
network_input::State network_input_snapshot(unsigned slot);
// Called only by the existing network owner, never by the scan or an HTTP handler.
void network_input_service();
