#pragma once
#include <Arduino.h>
// Loop-owner only: capture one source with full identity, then drain one bounded
// USB chunk per poll. A new request never replaces a pending record.
bool acquisition_record_begin(const char* request, const char* index);
bool acquisition_record_busy();
void acquisition_record_poll();
// Initialize once in setup before protocol workers. Getter only reads immutable
// bytes, so HTTP/diagnostic/record consumers share one boot identity safely.
void acquisition_record_boot_begin();
const char* acquisition_record_boot_id();
