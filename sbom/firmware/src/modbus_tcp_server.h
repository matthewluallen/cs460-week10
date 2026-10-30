#ifndef MODBUS_TCP_SERVER_H
#define MODBUS_TCP_SERVER_H
#include <Arduino.h>

#include "net_types.h"
#include "config.h"
#include "network_access.h"

// Modbus TCP Server with intentional vulnerabilities
// CWE-798: Hardcoded credentials
// CWE-319: Cleartext transmission
// CWE-306: Missing authentication for critical coil writes

class ModbusTCPServer {
public:
    ModbusTCPServer();
    bool start();
    void stop();
    void handle();  // Call this periodically from main loop

    // Coil/register access (for OpenPLC runtime)
    void topup_listeners();
    // Process one Modbus/TCP frame from a non-socket transport (USB serial gateway).
    static constexpr uint16_t MAX_ADU = 260;
    static constexpr uint32_t FRAME_TIMEOUT_MS = 2000;
    static constexpr uint32_t IDLE_TIMEOUT_MS = 30000;
    // Exactly one ADU. Invalid envelope/unit or insufficient output capacity
    // returns false, resp_len=0, without modifying the process image.
    bool handle_frame(const uint8_t* req, uint16_t len, uint8_t* resp,
                      size_t capacity, uint16_t& resp_len,
                      const char* source = "unknown", uint8_t unit = 1,
                      PlcAccessPeer peer = {});
    void set_coil(uint16_t addr, bool state);
    bool get_coil(uint16_t addr);
    void set_register(uint16_t addr, uint16_t val);
    uint16_t get_register(uint16_t addr);

private:
    NetServer server;
    NetClient clients[MODBUS_TCP_MAX_CLIENTS];
    bool client_active[MODBUS_TCP_MAX_CLIENTS];

    // Modbus data
    uint16_t coils[32];           // 512 coils
    uint16_t registers[256];      // 256 holding registers

    // Session tracking for CWE-330 (predictable PRNG)
    uint32_t session_ids[MODBUS_TCP_MAX_CLIENTS];

    struct ReceiveState {
        uint8_t data[MAX_ADU];
        uint16_t used = 0;
        uint32_t started = 0, last_activity = 0;
        char source[32] = {};
    } receive[MODBUS_TCP_MAX_CLIENTS];

    // Protocol handling
    void process_client(uint8_t idx);

    // Modbus function codes
    bool fc_read_coils(const uint8_t* req, uint8_t* resp, uint16_t& resp_len);
    bool fc_read_registers(const uint8_t* req, uint8_t* resp, uint16_t& resp_len);
    bool fc_write_single_coil(const uint8_t* req, uint8_t* resp, uint16_t& resp_len, const char* source);
    bool fc_write_single_register(const uint8_t* req, uint8_t* resp, uint16_t& resp_len, const char* source);
    bool fc_write_multiple_coils(const uint8_t* req, uint8_t* resp, uint16_t& resp_len, const char* source);
    bool fc_write_multiple_registers(const uint8_t* req, uint8_t* resp, uint16_t& resp_len, const char* source);

    // Authentication (CWE-798: hardcoded, no real check)
    bool authenticate(const char* user, const char* pass);

    // CWE-330: Predictable session token
    uint32_t generate_session_id(uint8_t client_idx);
};

extern ModbusTCPServer modbus_server;

// Cross-core PLC image lock (recursive). Wrap a group of related register/coil
// writes (e.g. the scan's analog or status publish) so readers never tear it.
void plc_image_lock();
void plc_image_unlock();

#endif // MODBUS_TCP_SERVER_H
