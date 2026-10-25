#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H
#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

// OTA firmware upload handler with CWE-494: no signature verification

class OTAHandler {
public:
    OTAHandler();
    bool start();
    void handle();

private:
    WebServer server;

    void handle_upload();
    void handle_upload_post();

    // CWE-494: No signature verification
    bool verify_firmware(const uint8_t* data, size_t len) {
        // Stub: accepts any binary without verification
        return true;
    }

    // Logging (CWE-22: vulnerable path traversal in logs)
    void log_upload(const char* source_ip, size_t size);
};

extern OTAHandler ota_handler;

#endif // OTA_HANDLER_H
