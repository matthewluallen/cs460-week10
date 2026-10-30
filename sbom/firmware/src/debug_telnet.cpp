#include "network_access.h"
#include "diagnostic_log.h"
#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include "debug_telnet.h"
#include "modbus_tcp_server.h"
#include "vulnerabilities.h"

DebugTelnet debug_telnet;

DebugTelnet::DebugTelnet() : server(DEBUG_TELNET_PORT) {}

bool DebugTelnet::start() {
#ifdef ENABLE_CWE_DEMOS
    server.begin();
    Serial.printf("[Telnet] Debug console started on port %d (CWE-489: no authentication)\n", DEBUG_TELNET_PORT);
    return true;
#else
    // Hardened (CWE-489 fixed): the unauthenticated debug console is never exposed.
    Serial.println("[Telnet] disabled in secure build (no unauthenticated debug console)");
    return false;
#endif
}

void DebugTelnet::handle() {
#ifndef ENABLE_CWE_DEMOS
    return;                               // secure build: telnet was never started (CWE-489 fixed)
#endif
    // This diagnostic shell has unscoped management operations. An enabled
    // point policy closes it in both profiles; use the trusted USB console.
    if (plc_project_host_acl_enabled()) {
        if (client) client.stop();
        auto pending = server.available(); if (pending) pending.stop();
        return;
    }
    // Accept incoming telnet connections
    if (!client) {
        client = server.available();
        if (client) {
            client.println("\n===== M5 StampLC Debug Console =====");
            client.println("Type 'help' for commands");
            client.println("WARNING: This is an unauthenticated debug interface (CWE-489)");
            client.print("> ");
        }
    }

    // Process telnet input
    if (client && client.connected()) {
        if (client.available()) {
            String cmd = client.readStringUntil('\n');
            cmd.trim();

            if (cmd.length() > 0) {
                diagnostic_log_printf(diagnostics::Origin::Application,"[Telnet] Command: %s\n", cmd.c_str());
                process_command(cmd);
                client.print("> ");
            }
        }
    } else if (client) {
        client.stop();
    }
}

void DebugTelnet::stop() {
    if (client) {
        client.stop();
    }
}

void DebugTelnet::process_command(const String& cmd) {
    PlcNetworkImageGuard guard;
    if (plc_project_host_acl_enabled()) { client.stop(); return; }
    if (cmd == "help") {
        client.println("\n--- Available Commands ---");
        client.println("status            - Show device status");
        client.println("relay [0-3] [0|1] - Control relay output");
        client.println("di [0-7]          - Read digital input");
        client.println("ping <ip>         - Ping IP (CWE-78: Command injection!)");
        client.println("gpio <pin> <val>  - Set GPIO");
        client.println("reboot            - Restart device");
        client.println("");
    }
    else if (cmd == "status") {
        client.printf("Uptime: %lld ms\n", esp_timer_get_time() / 1000);
        client.printf("Free heap: %u bytes\n", esp_get_free_heap_size());
        client.printf("RSSI: %d dBm\n", WiFi.RSSI());
    }
    else if (cmd.startsWith("relay")) {
        // Parse: relay <addr> <state>
        int space1 = cmd.indexOf(' ');
        int space2 = cmd.indexOf(' ', space1 + 1);
        if (space2 > 0) {
            uint8_t addr = cmd.substring(space1 + 1, space2).toInt();
            bool state = cmd.substring(space2 + 1).toInt() ? true : false;
            modbus_server.set_coil(addr, state);
            client.printf("Relay %d set to %d\n", addr, state ? 1 : 0);
        }
    }
    else if (cmd.startsWith("di")) {
        int space = cmd.indexOf(' ');
        if (space > 0) {
            uint8_t addr = cmd.substring(space + 1).toInt();
            bool val = modbus_server.get_coil(100 + addr);  // DI mapped to coils 100-107
            client.printf("DI %d = %d\n", addr, val ? 1 : 0);
        }
    }
    else if (cmd.startsWith("ping")) {
        int space = cmd.indexOf(' ');
        if (space > 0) {
            String target = cmd.substring(space + 1);
#ifdef ENABLE_CWE_DEMOS
            // CWE-78: Command injection - unsanitized input to system()
            client.printf("Pinging %s...\n", target.c_str());
            diagnostic_ping(target.c_str());  // CWE-78 vulnerable function
#else
            // Hardened (CWE-78 fixed): accept only a bare dotted-quad IPv4; reject
            // anything with shell metacharacters before it can reach system().
            bool valid = target.length() > 0 && target.length() <= 15;
            for (size_t i = 0; valid && i < target.length(); i++) {
                char c = target[i];
                if (!((c >= '0' && c <= '9') || c == '.')) valid = false;
            }
            if (valid) { client.printf("Pinging %s...\n", target.c_str()); diagnostic_ping(target.c_str()); }
            else       client.println("rejected: target must be a bare IPv4 address");
#endif
        }
    }
    else if (cmd == "reboot") {
        client.println("Rebooting...");
        delay(500);
        ESP.restart();
    }
    else if (cmd.length() > 0) {
        client.println("Unknown command. Type 'help' for list.");
    }
}
