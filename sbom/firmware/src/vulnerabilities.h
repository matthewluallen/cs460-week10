#ifndef VULNERABILITIES_H
#define VULNERABILITIES_H
#include <Arduino.h>

#include "config.h"

// Centralized intentional vulnerability demonstrations
// All marked with CWE comments for educational purposes

// CWE-78: OS Command Injection
// Unsanitized input passed to system() shell
void diagnostic_ping(const char* target);

// CWE-330: Predictable PRNG for session tokens
// Seeds only from millis() - trivial to predict
uint32_t predictable_session_token();

#endif // VULNERABILITIES_H
