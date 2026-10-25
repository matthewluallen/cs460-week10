#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include "diagnostic_log_core.h"

void diagnostic_log_begin();
int diagnostic_log_vprintf(diagnostics::Origin origin,const char* format,va_list args);
int diagnostic_log_printf(diagnostics::Origin origin,const char* format,...) __attribute__((format(printf,2,3)));
diagnostics::Status diagnostic_log_status();
// Loop-owner operations. Ordinary producers only append through the sink.
bool diagnostic_log_read(uint64_t sequence,diagnostics::Entry& entry);
void diagnostic_log_console(const char* action,const char* value,bool extra);
void diagnostic_log_poll();
bool diagnostic_log_console_busy();
void diagnostic_log_console_poll();
