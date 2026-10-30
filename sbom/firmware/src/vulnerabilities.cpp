#include "diagnostic_log.h"
#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include "vulnerabilities.h"
#include <cstdlib>

// CWE-78: OS Command Injection
// Diagnostic ping function that passes unsanitized user input to system()
// Student can inject shell metacharacters: "; id", "; cat /etc/passwd", etc.
void diagnostic_ping(const char* target) {
#if DIAGNOSTIC_PING_UNSAFE
    // CWE-78: VULNERABLE - no input sanitization
    // Target could be "1.1.1.1; id" and the semicolon would execute arbitrary commands
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ping -c 4 %s", target);  // Unsanitized!

    diagnostic_log_printf(diagnostics::Origin::Application,"[CWE-78] Executing: %s\n", cmd);

    // WARNING: On ESP32, system() doesn't actually work (no shell)
    // But this demonstrates the vulnerability pattern
    // In a real embedded Linux system, this WOULD execute arbitrary commands
    // For teaching purposes, we'll simulate:

    if (strstr(target, ";") != NULL || strstr(target, "|") != NULL || strstr(target, "&") != NULL) {
        diagnostic_log_printf(diagnostics::Origin::Application,"[CWE-78] DETECTED: Command injection attempt!");
        diagnostic_log_printf(diagnostics::Origin::Application,"[CWE-78] Injected command: %s\n", strstr(target, ";") ? strstr(target, ";") + 1 :
                                                          (strstr(target, "|") ? strstr(target, "|") + 1 :
                                                           strstr(target, "&") + 1));
    }
#else
    // Secure version: validate target is an IP/hostname
    // (not used in vulnerable build)
    if (strlen(target) > 255) {
        diagnostic_log_printf(diagnostics::Origin::Application,"[Secure] Target too long");
        return;
    }

    // Only allow alphanumeric, dots, hyphens
    for (int i = 0; target[i]; i++) {
        char c = target[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '.' || c == '-')) {
            diagnostic_log_printf(diagnostics::Origin::Application,"[Secure] Invalid character in target: %c\n", c);
            return;
        }
    }
#endif
}

// CWE-330 / CWE-337: Predictable PRNG for session tokens
// Seeded only from millis() - extremely weak entropy
// Student can observe millis() and predict all future tokens
uint32_t predictable_session_token() {
#if SESSION_TOKEN_PREDICTABLE
    // CWE-330: VULNERABLE - only 65536 possible values
    // Seed is deterministic based on millis() which is known/observable
    uint32_t seed = millis() % (1 << SESSION_TOKEN_ENTROPY_BITS);
    uint32_t token = seed;

    // Simple LCG (not even a good PRNG)
    token = (token * 1103515245 + 12345) & 0x7fffffff;

    return token;
#else
    // Secure version: use hardware RNG
    // uint32_t token = esp_random();
    return 0;
#endif
}

// CWE-1393: Default/hardcoded credentials (related to CWE-798)
// The Modbus server accepts the hardcoded password defined in config.h
// Student can use: username="admin", password="openplc"
void check_default_credentials_example() {
    // This is just documentation of the vulnerability. The Modbus server
    // performs NO authentication at all - every request is served (CWE-306).

    diagnostic_log_printf(diagnostics::Origin::Application,"[CWE-798/1393] Hardcoded credentials:");
    diagnostic_log_printf(diagnostics::Origin::Application,"  User: %s\n", MODBUS_HARDCODED_USER);
    diagnostic_log_printf(diagnostics::Origin::Application,"  Pass: %s\n", MODBUS_HARDCODED_PASS);
    diagnostic_log_printf(diagnostics::Origin::Application,"[CWE-798] Modbus server accepts any password!");
}
