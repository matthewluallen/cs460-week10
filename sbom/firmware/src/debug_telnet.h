#ifndef DEBUG_TELNET_H
#define DEBUG_TELNET_H
#include <Arduino.h>

#include <WiFi.h>
#include "config.h"

// Debug telnet interface - CWE-489: Debug code in production
// Exposes unauthenticated command shell on port 23

class DebugTelnet {
public:
    DebugTelnet();
    bool start();
    void handle();
    void stop();

private:
    WiFiServer server;
    WiFiClient client;

    void process_command(const String& cmd);
};

extern DebugTelnet debug_telnet;

#endif // DEBUG_TELNET_H
