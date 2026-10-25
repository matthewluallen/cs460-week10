// plc_diag.h - shipped student/field diagnostics for the StamPLC teaching kit.
//
// Everything here stays in the delivered firmware so a student can self-diagnose
// a misbehaving controller, and so we can remotely troubleshoot an issue they hit:
// the `support` console command emits one copy-pasteable bundle (health snapshot +
// reset reason + the RTC "black box" of the last known state before a crash) that
// the student sends us. A real ESP-IDF flash core-dump is not available in this
// Arduino build, so the black box (RTC_NOINIT, survives a panic reboot) is our
// post-mortem: it tells us what the controller was doing when it died.
#pragma once
#include <Arduino.h>

void        plc_diag_begin();               // setup(): latch reset reason + roll the black box
void        plc_diag_loop(uint32_t now);    // loop(): cheap throttled black-box checkpoint
void        plc_diag_note_cmd(const char* c); // record last console command into the black box
void        plc_diag_note_phase(const char* p); // optional coarse phase breadcrumb
bool        plc_diag_console(const char* cmd, char* a1, char* a2); // true if it handled the command
const char* plc_diag_reset_reason_str();    // human string for the last reset
bool        plc_diag_last_was_crash();      // true if the previous boot ended in a panic/WDT
void        plc_diag_help();                // one-liner help rows for the console `help`
