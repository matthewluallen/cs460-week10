#pragma once
#include <stdint.h>
#include <limits.h>

namespace timing {
enum LoopPhase { FRONT_SERVICES, ACQUISITION, SCAN_SLOT, NETWORK_SERVICES, TELEMETRY, DISPLAY_PHASE, PHASE_COUNT };
enum PanelOperation { BUTTON_UPDATE, BOOT_RENDER, NORMAL_RENDER, PANEL_OPERATION_COUNT };
// Source boot-clock microseconds, never host time or a real-time guarantee.
struct Metric {
    uint64_t count=0,last_us=0,min_us=0,max_us=0,over_50ms=0;
    bool saturated=false;
    void add(uint64_t value) {
        if(!count||value<min_us)min_us=value;
        if(value>max_us)max_us=value;
        last_us=value;
        if(count!=UINT64_MAX)++count;else saturated=true;
        if(value>50000){if(over_50ms!=UINT64_MAX)++over_50ms;else saturated=true;}
    }
};
struct Snapshot {
    Metric loop_busy,loop_period,scan_busy,scan_period;
    Metric phases[PHASE_COUNT];
    Metric panel[PANEL_OPERATION_COUNT];
    uint64_t captured_us=0,clock_errors=0;
    bool loop_pending=false,scan_pending=false;
};
class Tracker {
    Snapshot state;
    uint64_t loop_start=0,scan_start=0;
    bool seen_loop=false,seen_scan=false;
    unsigned next_phase=0; uint64_t phase_start=0;
    void error(){if(state.clock_errors!=UINT64_MAX)++state.clock_errors;}
    void begin(uint64_t now,uint64_t& start,bool& seen,bool& pending,Metric& period) {
        if(pending)error(); // a missing end must not fabricate a duration
        if(seen){if(now>=start)period.add(now-start);else error();}
        start=now;seen=true;pending=true;
    }
    void end(uint64_t now,uint64_t start,bool& pending,Metric& busy) {
        if(!pending||now<start)error();else busy.add(now-start);
        pending=false;
    }
public:
    void loop_begin(uint64_t now){begin(now,loop_start,seen_loop,state.loop_pending,state.loop_period);next_phase=0;phase_start=now;}
    void loop_phase(unsigned phase,uint64_t now){
        if(!state.loop_pending||phase!=next_phase||phase>=PHASE_COUNT||now<phase_start){error();return;}
        state.phases[phase].add(now-phase_start);phase_start=now;++next_phase;
    }
    void loop_end(uint64_t now){
        if(next_phase==DISPLAY_PHASE)loop_phase(DISPLAY_PHASE,now);
        else if(next_phase!=0&&next_phase!=PHASE_COUNT)error();
        end(now,loop_start,state.loop_pending,state.loop_busy);
    }
    void panel_operation(unsigned operation,uint64_t start,uint64_t end){
        if(operation>=PANEL_OPERATION_COUNT||end<start){error();return;}
        state.panel[operation].add(end-start);
    }
    void scan_begin(uint64_t now){begin(now,scan_start,seen_scan,state.scan_pending,state.scan_period);}
    void scan_end(uint64_t now){end(now,scan_start,state.scan_pending,state.scan_busy);}
    Snapshot snapshot(uint64_t now) const {auto copy=state;copy.captured_us=now;return copy;}
};
}
