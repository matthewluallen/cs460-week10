#include "loop_timing.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <esp_timer.h>
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
timing::Tracker tracker;
struct Guard {Guard(){portENTER_CRITICAL(&mux);}~Guard(){portEXIT_CRITICAL(&mux);}};
}
// No allocation, serialization, image lock or I/O while retaining metrics.
void plc_timing_loop_begin(){Guard guard;tracker.loop_begin(esp_timer_get_time());}
void plc_timing_loop_phase(timing::LoopPhase phase){Guard guard;tracker.loop_phase(phase,esp_timer_get_time());}
void plc_timing_loop_end(){Guard guard;tracker.loop_end(esp_timer_get_time());}
void plc_timing_scan_begin(){Guard guard;tracker.scan_begin(esp_timer_get_time());}
void plc_timing_scan_end(){Guard guard;tracker.scan_end(esp_timer_get_time());}
timing::Snapshot plc_timing_snapshot(){Guard guard;return tracker.snapshot(esp_timer_get_time());}

void plc_timing_panel_operation(timing::PanelOperation operation,uint64_t start,uint64_t end){Guard guard;tracker.panel_operation(operation,start,end);}
