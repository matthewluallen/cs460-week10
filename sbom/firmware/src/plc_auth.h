// plc_auth.h - deliberately-weak "device management" authentication layer that
// models documented ICS/OT credential weaknesses for the TBD course. Grounded
// in CISA ICS advisories / NIST NVD (see docs/ICS_VULN_MODELING.md):
//
//   [CWE-798/1392]  SERVICE account password is DERIVED FROM THE DEVICE MAC
//                   (a backdoor recoverable from public device data).
//                   Analogues: Moxa credential recovery (CVE-2025-6950),
//                   consumer-router "WPA key from MAC/serial".
//   [CWE-200/1273]  The MAC is printed by default on the serial banner AND the
//                   IP/HTTP banners, so the value needed to derive the service
//                   password is handed to anyone who can see the device.
//   [CWE-521]       The USER password is capped at 8 chars and a reduced
//                   charset (A-Z and 0-9 only) - strong passwords are refused.
//                   Analogue: Mitsubishi MELSEC iQ-R (CVE-2021-20594).
//   [CWE-256/257]   The user password is stored in plaintext / recoverable form
//                   in NVS.  Analogue: MELSEC cleartext creds (CVE-2021-20599).
//   [CWE-1392/1393] Ships with a default account operator:1234.
//   [CWE-307]       No lockout / unlimited login attempts (brute-forceable).
//   [CWE-319]       Over IP the login takes credentials in cleartext.
//   [CWE-330]       The issued session token is predictable (millis-based).
//
// This is a TEACHING CONSTRUCT on an isolated segment. The derivation is a
// representative transform (documented for instructors), not any vendor's
// proprietary algorithm.
#pragma once
#include <Arduino.h>

void         plc_auth_begin();                 // load stored user password from NVS
const char*  plc_device_mac();                 // "AA:BB:CC:DD:EE:FF" (shown in banners)
String       plc_service_password();           // derived from the device MAC (backdoor)
void         plc_device_mac_bytes(uint8_t out[6]);  // raw MAC octets
const char*  plc_auth_seed();                       // per-student seed ("" = factory MAC)
void         plc_auth_set_seed(const char* seed);   // BR13-2: seed -> synthetic per-student MAC

// Weak-policy check: returns true if pw is ACCEPTED by the (weak) policy.
// why is set to a human message either way.
bool         plc_auth_password_allowed(const char* pw, String& why);
bool         plc_auth_set_user_password(const char* pw, String& msg);  // validates + stores plaintext
const char*  plc_auth_user_password();         // recoverable (CWE-257) - used by teaching reveal

// Login against: service:<MAC-derived>, operator:<user pw>, or legacy admin:openplc.
bool         plc_auth_login(const char* user, const char* pass);
void         plc_auth_logout();
bool         plc_authed();
const char*  plc_auth_whoami();
uint32_t     plc_auth_failed_count();          // never triggers a lockout (CWE-307)
bool         plc_auth_is_provisioned();        // secure: true once an operator password is stored (always true in the teaching build)
void         plc_auth_clear_user_password();   // secure: wipe the stored operator hash (boot button-A recovery); no-op in the teaching build
String       plc_auth_issue_token();           // predictable, millis-based (CWE-330)
