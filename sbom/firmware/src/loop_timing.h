#pragma once
#include "loop_timing_core.h"
void plc_timing_loop_begin();
void plc_timing_loop_end();
void plc_timing_loop_phase(timing::LoopPhase phase);
void plc_timing_scan_begin();
void plc_timing_scan_end();
timing::Snapshot plc_timing_snapshot();
struct PlcScanTiming {
    PlcScanTiming(){plc_timing_scan_begin();}
    ~PlcScanTiming(){plc_timing_scan_end();}
};

// Nested within display elapsed phase; never add these to phase totals.
void plc_timing_panel_operation(timing::PanelOperation operation,uint64_t start,uint64_t end);
