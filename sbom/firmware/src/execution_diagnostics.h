// Boot-local execution evidence. No scheduler, output, allocation or clock I/O.
// The device adapter owns one instance and serializes access with the PLC image.
#pragma once
#include <cstdint>
#include <climits>

namespace execution {
constexpr uint64_t PeriodUs = 50000;
constexpr uint32_t ScanFaultUs = 100000;
constexpr uint32_t WatchdogTimeoutMs = 8000;

enum class Origin : uint8_t { Scheduled, Maintenance };
enum class Phase : uint8_t { OutsideScan, Acquisition, BindingRefresh, Inputs, Solve, Outputs, Readback, FaultHandling };
inline const char* name(Origin value) { return value == Origin::Scheduled ? "scheduled" : "maintenance"; }
inline const char* name(Phase value) {
    switch (value) {
    case Phase::Acquisition: return "acquisition";
    case Phase::BindingRefresh: return "binding_refresh";
    case Phase::Inputs: return "input_and_evidence";
    case Phase::Solve: return "solve";
    case Phase::Outputs: return "outputs_and_expansion_readback";
    case Phase::Readback: return "base_readback";
    case Phase::FaultHandling: return "status_and_fault_handling";
    default: return "outside_scan";
    }
}
inline void add(uint64_t& value, uint64_t amount, bool& saturated) {
    if (UINT64_MAX - value < amount) { value = UINT64_MAX; saturated = true; }
    else value += amount;
}
struct Range {
    uint64_t count = 0, last_us = 0, min_us = 0, max_us = 0;
    bool saturated = false;
    void observe(uint64_t value) {
        if (!count || value < min_us) min_us = value;
        if (!count || value > max_us) max_us = value;
        last_us = value; add(count, 1, saturated);
    }
};
struct Jitter {
    uint64_t count = 0;
    int64_t last_us = 0, min_us = 0, max_us = 0;
    bool saturated = false;
    void observe(uint64_t period) {
        int64_t value;
        if (period < PeriodUs) value = -int64_t(PeriodUs - period);
        else if (period - PeriodUs > uint64_t(INT64_MAX)) { value = INT64_MAX; saturated = true; }
        else value = int64_t(period - PeriodUs);
        if (!count || value < min_us) min_us = value;
        if (!count || value > max_us) max_us = value;
        last_us = value; add(count, 1, saturated);
    }
};
struct Scan {
    bool available = false, period_available = false;
    Origin origin = Origin::Maintenance;
    uint64_t sequence = 0, started_us = 0, completed_us = 0, elapsed_us = 0;
    uint32_t fault_check_elapsed_us = 0;
    uint64_t period_us = 0, missed_releases = 0;
    int64_t jitter_us = 0;
};
struct Pending {
    bool active = false;
    Origin origin = Origin::Maintenance;
    Phase phase = Phase::OutsideScan;
    uint64_t sequence = 0, started_us = 0;
};
struct Snapshot {
    bool saturated = false;
    uint64_t completed = 0, scheduled = 0, maintenance = 0, clock_errors = 0;
    uint64_t late_releases = 0, missed_releases = 0;
    uint64_t scheduled_budget_misses = 0, consecutive_budget_misses = 0, max_consecutive_budget_misses = 0;
    uint64_t hard_limit_exceedances = 0;
    Range elapsed, scheduled_period;
    Jitter scheduled_jitter;
    Pending pending;
    Scan last;
};
class Tracker {
    Snapshot state;
    Scan candidate;
    bool seen_scheduled = false;
    uint64_t previous_scheduled = 0;
    void error() { add(state.clock_errors, 1, state.saturated); }
public:
    void begin(uint64_t now, Origin origin) {
        // Never fabricate a completed scan when its end was not observed.
        if (state.pending.active) error();
        candidate = Scan{};
        candidate.origin = origin;
        candidate.sequence = state.completed;
        add(candidate.sequence, 1, state.saturated);
        candidate.started_us = now;
        state.pending = {true, origin, Phase::Acquisition, candidate.sequence, now};
        if (origin != Origin::Scheduled) return;
        if (seen_scheduled) {
            if (now >= previous_scheduled) {
                candidate.period_available = true;
                candidate.period_us = now - previous_scheduled;
                state.scheduled_period.observe(candidate.period_us);
                state.scheduled_jitter.observe(candidate.period_us);
                candidate.jitter_us = state.scheduled_jitter.last_us;
                if (candidate.period_us > PeriodUs) add(state.late_releases, 1, state.saturated);
                // Actual-start anchored scheduler; no catch-up output bursts.
                // At 100 ms between starts, one 50 ms release was omitted.
                if (candidate.period_us >= PeriodUs)
                    candidate.missed_releases = candidate.period_us / PeriodUs - 1;
                add(state.missed_releases, candidate.missed_releases, state.saturated);
            } else error();
        }
        seen_scheduled = true; previous_scheduled = now;
    }
    void phase(Phase value) { if (state.pending.active) state.pending.phase = value; }
    void finish(uint64_t now, uint32_t fault_check_elapsed_us) {
        if (!state.pending.active) { error(); return; }
        state.pending = Pending{};
        if (now < candidate.started_us) { error(); return; }
        candidate.available = true; candidate.completed_us = now;
        candidate.elapsed_us = now - candidate.started_us;
        candidate.fault_check_elapsed_us = fault_check_elapsed_us;
        state.elapsed.observe(candidate.elapsed_us);
        add(state.completed, 1, state.saturated);
        if (candidate.origin == Origin::Scheduled) {
            add(state.scheduled, 1, state.saturated);
            if (candidate.elapsed_us > PeriodUs) {
                add(state.scheduled_budget_misses, 1, state.saturated);
                add(state.consecutive_budget_misses, 1, state.saturated);
                if (state.consecutive_budget_misses > state.max_consecutive_budget_misses)
                    state.max_consecutive_budget_misses = state.consecutive_budget_misses;
            } else state.consecutive_budget_misses = 0;
        } else add(state.maintenance, 1, state.saturated);
        if (fault_check_elapsed_us > ScanFaultUs) add(state.hard_limit_exceedances, 1, state.saturated);
        state.last = candidate;
    }
    const Snapshot& snapshot() const { return state; }
};

enum class WatchdogError : uint8_t { None, Configuration, Subscription, Feed };
inline const char* name(WatchdogError value) {
    switch (value) {
    case WatchdogError::Configuration: return "configuration";
    case WatchdogError::Subscription: return "subscription_check";
    case WatchdogError::Feed: return "feed";
    default: return "none";
    }
}
struct Watchdog {
    bool setup_attempted = false, reconfigure_attempted = false, saturated = false;
    int init_result = 0, configuration_result = 0, subscription_result = 0, status_result = 0, last_feed_result = 0;
    uint64_t setup_us = 0, feed_attempts = 0, feed_successes = 0, feed_errors = 0;
    uint64_t last_feed_attempt_us = 0, last_feed_success_us = 0;
    WatchdogError first_error = WatchdogError::None;
    uint64_t first_error_us = 0;
    int first_error_result = 0;
    void note_error(WatchdogError kind, int result, uint64_t now) {
        if (first_error != WatchdogError::None) return;
        first_error = kind; first_error_result = result; first_error_us = now;
    }
    void setup(int init, bool reconfigured, int configured, int subscribed, int status, uint64_t now) {
        setup_attempted = true; setup_us = now; init_result = init;
        reconfigure_attempted = reconfigured; configuration_result = configured;
        subscription_result = subscribed; status_result = status;
        if (configured != 0) note_error(WatchdogError::Configuration, configured, now);
        else if (status != 0) note_error(WatchdogError::Subscription, status, now);
    }
    void feed(int result, uint64_t now) {
        add(feed_attempts, 1, saturated); last_feed_attempt_us = now; last_feed_result = result;
        if (result == 0) { add(feed_successes, 1, saturated); last_feed_success_us = now; }
        else { add(feed_errors, 1, saturated); note_error(WatchdogError::Feed, result, now); }
    }
};
} // namespace execution
