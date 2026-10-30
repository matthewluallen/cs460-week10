// A one-shot startup request, separate from the normal PLC program. Acquisition
// can wait for readiness in PROGRAM; this state machine never executes VM code.
#pragma once
#include <stdint.h>

namespace project {
enum class StartupMode : uint8_t { Program, Run };
enum class StartupState : uint8_t { Disabled, AwaitingReboot, Pending, Started, Cancelled, Blocked };
constexpr uint32_t StartupTimeoutMs = 30000;
inline const char* startup_mode_name(StartupMode mode) { return mode == StartupMode::Run ? "run" : "program"; }
inline const char* startup_state_name(StartupState state) {
    switch (state) {
    case StartupState::Disabled: return "disabled";
    case StartupState::AwaitingReboot: return "awaiting_reboot";
    case StartupState::Pending: return "pending";
    case StartupState::Started: return "started";
    case StartupState::Cancelled: return "cancelled";
    case StartupState::Blocked: return "blocked";
    }
    return "blocked";
}
struct StartupRequest {
    StartupState state = StartupState::Disabled;
    uint32_t began_ms = 0, elapsed_ms = 0;
    const char* reason = "program_default";

    void reset(StartupMode mode, bool restored, uint32_t now) {
        began_ms = now; elapsed_ms = 0;
        state = mode == StartupMode::Program ? StartupState::Disabled :
                restored ? StartupState::Pending : StartupState::AwaitingReboot;
        reason = state == StartupState::Disabled ? "program_default" :
                 state == StartupState::Pending ? "waiting_for_readiness" : "restart_required";
    }
    void cancel(const char* why, uint32_t now) {
        if (state != StartupState::Pending) return;
        elapsed_ms = now - began_ms;
        state = StartupState::Cancelled; reason = why;
    }
    // Consumes the request before the caller invokes the ordinary guarded mode
    // transition. A rejected transition cannot be retried by a later scan.
    bool consume(uint32_t now, bool ready, bool fault) {
        if (state != StartupState::Pending) return false;
        elapsed_ms = now - began_ms;
        if (fault || elapsed_ms >= StartupTimeoutMs) {
            state = StartupState::Blocked; reason = fault ? "latched_fault" : "startup_timeout";
            return false;
        }
        if (!ready) return false;
        state = StartupState::Started; reason = "run_requested"; return true;
    }
    void finish(bool entered_run) {
        state = entered_run ? StartupState::Started : StartupState::Blocked;
        reason = entered_run ? "run_entered" : "startup_admission_refused";
    }
    uint32_t elapsed(uint32_t now) const { return state == StartupState::Pending ? now - began_ms : elapsed_ms; }
};
} // namespace project
