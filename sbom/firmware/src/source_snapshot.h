#pragma once
#include <Arduino.h>
#include "process_bindings.h"
#include "plc_core.h"
struct PlcSourceSnapshot {
    PlcBindingsSnapshot bindings;
    PlcScanSnapshot last_scan;
    plc::Instr program[32];
    int program_count=0,faults=0,forces=0;
    char mode[12]{};
    uint64_t captured_us=0;
};
// Captures the program, resolved binding copy and mode in one image-lock interval.
// Acquisition/bus state is never read by a protocol worker.
bool plc_source_capture(PlcSourceSnapshot& out);
// Both serializers consume the captured copy after releasing the image lock.
String plc_source_program_json(const PlcSourceSnapshot& snapshot);
String plc_source_json(const PlcSourceSnapshot& snapshot);
String plc_source_json();

// Separate bounded scan response; source binding response retains its capacity.
String plc_scan_json(const PlcSourceSnapshot& snapshot);
String plc_scan_json();

String plc_loop_phases_json();
