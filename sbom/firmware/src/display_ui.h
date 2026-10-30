// display_ui.h - on-device LCD panel (TBD StamPLC): boot logo + live home
// screen (mode hero + I/O) + 3-button navigation (RUN/PROG/REM mode change).
// First increment of firmware/LCD_UX_SPEC.md; renders on M5StamPLC.Display.
#pragma once
void display_ui_boot();   // call once in setup() AFTER io.init() (display is up by then)
void display_ui_tick();   // call from loop(); refreshes buttons + renders (self-throttled)
void display_ui_inject(char btn); // inject an A/B/C panel button press (UI drives the on-device screen)
bool display_ui_btnA_held_at_boot(); // true if button A is physically held (maintainer boot recovery)

#include "display_capture_core.h"
// Loop-task-only APIs. Optional candidate profile; unavailable on ordinary builds.
display_capture::Result display_ui_capture_request(display_capture::Ticket& ticket);
display_capture::Result display_ui_capture_manifest(const display_capture::Ticket&,display_capture::Manifest&);
display_capture::Result display_ui_capture_read(const display_capture::Ticket&,size_t offset,size_t length,uint8_t* out,size_t capacity);
display_capture::Result display_ui_capture_cancel(const display_capture::Ticket&);

const char* display_ui_capture_status(); // static availability reason, not a hardware observation
