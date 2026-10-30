#include "network_access.h"
#include "diagnostic_log.h"
#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include "ota_handler.h"
#include <LittleFS.h>
#include <Update.h>

OTAHandler ota_handler;

OTAHandler::OTAHandler() : server(OTA_HTTP_PORT) {}

bool OTAHandler::start() {
    // Initialize filesystem for logging
    if (!LittleFS.begin(true)) {  // format-on-fail: self-heal a corrupted FS instead of bricking the device
        Serial.println("[OTA] LittleFS init failed");
        return false;
    }

    // Create logs directory
    LittleFS.mkdir(LOG_DIR);

    server.on(OTA_ENDPOINT, HTTP_POST, [this]() { handle_upload_post(); });
    server.begin();

    Serial.printf("[OTA] HTTP server started on port %d\n", OTA_HTTP_PORT);
    return true;
}

void OTAHandler::handle() {
    server.handleClient();
}

void OTAHandler::handle_upload_post() {
    PlcNetworkImageGuard guard;
    if (plc_project_host_acl_enabled()) {
        server.send(403, "application/json", PLC_ACL_ERROR); return;
    }
    // CWE-494: No signature verification on uploaded firmware
    if (server.method() == HTTP_POST) {
        if (server.hasArg("plain")) {
            String firmware_data = server.arg("plain");

            // CWE-494: Accept any binary without checking signature/hash
            if (verify_firmware((uint8_t*)firmware_data.c_str(), firmware_data.length())) {
                // Log the upload (CWE-22: vulnerable path traversal here)
                String source_ip = server.client().remoteIP().toString();
                log_upload(source_ip.c_str(), firmware_data.length());

                // Flash the firmware
                if (Update.begin(firmware_data.length())) {
                    if (Update.write((uint8_t*)firmware_data.c_str(), firmware_data.length()) == firmware_data.length()) {
                        if (Update.end()) {
                            server.send(200, "text/plain", "Firmware updated successfully");
                            delay(1000);
                            ESP.restart();
                        } else {
                            server.send(400, "text/plain", "Update failed");
                        }
                    } else {
                        server.send(400, "text/plain", "Write failed");
                    }
                } else {
                    server.send(400, "text/plain", "Update begin failed");
                }
            } else {
                server.send(400, "text/plain", "Invalid firmware");
            }
        }
    }
}

// CWE-22: Path traversal vulnerability in log file access
void OTAHandler::log_upload(const char* source_ip, size_t size) {
    // Create log entry with timestamp
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);

    // Log filename: vulnerable to path traversal when accessed via web HMI
    // Student can request "/logs?file=../../config.txt" to read arbitrary files
    char log_file[128];
    snprintf(log_file, sizeof(log_file), "%s/ota_%04d%02d%02d_%02d%02d%02d.txt",
             LOG_DIR,
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    // Write log entry
    File f = LittleFS.open(log_file, "w");
    if (f) {
        f.printf("Source: %s\n", source_ip);
        f.printf("Size: %lu bytes\n", size);
        f.printf("Timestamp: %s\n", asctime(timeinfo));
        f.close();

        diagnostic_log_printf(diagnostics::Origin::Application,"[OTA] Logged to %s\n", log_file);
    }
}
