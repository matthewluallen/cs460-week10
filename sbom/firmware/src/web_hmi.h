#ifndef WEB_HMI_H
#define WEB_HMI_H
#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

// Web HMI with multiple vulnerabilities
// CWE-22: Path traversal in log viewer
// CWE-287/290: No authentication on session cookie
// CWE-330: Predictable PRNG for session tokens

class WebHMI {
public:
    WebHMI();
    bool start();
    void handle();

private:
    WebServer server;

    void handle_root();
    void handle_coils();
    void handle_registers();
    void handle_logs();      // CWE-22: Path traversal here
    void handle_api();
    void handle_points();   // JSON: mode, scan, faults, forces + all named points
    void handle_set();      // set a writable point (logged source=WEB)
    void handle_force();    // force / unforce a coil or DI
    void handle_mode();     // get / set RUN / PROGRAM
    void handle_hlog();     // the source-attributed audit log as JSON
    void handle_config();   // GET/POST network+NTP config, "protected" by a DEFAULT cred (CWE-798/1392)


    // Simple HTML page
    String get_html_page();
};

extern WebHMI web_hmi;

#endif // WEB_HMI_H
