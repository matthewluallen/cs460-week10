#pragma once
#include <Arduino.h>
#include "plc_engine.h"
#include "process_bindings_core.h"
#include "process_input_gate.h"
// These functions serialize published state with the recursive PLC image lock.
void plc_bindings_begin();
// Loop/scan owner only: the acquisition engine is owned by that same task.
void plc_bindings_refresh();
void plc_bindings_invalidate();
bool plc_bindings_ready(const plc::Instr* program,int count,bool arm);
void plc_bindings_scan(plc::Engine& engine,const plc::Instr* program,int count,bool executing);
// Optional compare-and-set token protects the interactive assignment workflow.
// It is a concurrency fence, not authentication or authorization.
struct PlcBindingVersion {uint32_t generation=0;char boot_id[33]{};char config_sha256[65]{};};
bool plc_bindings_load(const char* json,size_t size,const PlcBindingVersion* expected=nullptr);
// Fixed-size immutable copy: no heap, JSON, hashing or hardware I/O under lock.
struct PlcBindingsSnapshot {
    bindings::Config config;
    bindings::Sample samples[bindings::Count];
    network_input::State network[bindings::Count];
    uint32_t generation=0,published_ms=0,observed_ms=0;
    uint8_t status=0;
    char config_sha256[65]{};
    char boot_id[33]{};
};
void plc_bindings_capture(PlcBindingsSnapshot& out);
String plc_bindings_json(const PlcBindingsSnapshot& snapshot);
String plc_bindings_json();
void plc_bindings_console(const char* argument);

// Compact copied publication for genuine generated logic; no VM program required.
void plc_bindings_process_capture(process_input::Snapshot& out);
