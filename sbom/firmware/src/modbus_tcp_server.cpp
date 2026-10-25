#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include "modbus_tcp_server.h"
#include "eth_net.h"
#include "io_expansion.h"
#include "m5stamplc_io.h"
#include "plc_core.h"
#include "plc_tags.h"
#ifdef ENABLE_ETHERNET
#include <utility/w5100.h>
#endif

ModbusTCPServer modbus_server;

// Cross-core PLC image lock (board review #13, condition 1). The Ethernet comms
// task (core 0), the USB gateway + PLC scan (core 1) all touch the coil/register
// image. A RECURSIVE MUTEX (blocking, not a spinlock) makes each single access
// atomic AND lets a whole Modbus transaction (multi-register FC3/FC16/FC23) and
// the scan's grouped publish be locked at transaction/group scope, so no reader
// ever observes a torn multi-register transaction - without busy-spinning a core
// (which would inject jitter into the 37 ms scan).
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static SemaphoreHandle_t s_image_mtx = nullptr;
void plc_image_lock() {
    if (!s_image_mtx) s_image_mtx = xSemaphoreCreateRecursiveMutex();  // first use is single-threaded (setup)
    xSemaphoreTakeRecursive(s_image_mtx, portMAX_DELAY);
}
void plc_image_unlock() {
    if (s_image_mtx) xSemaphoreGiveRecursive(s_image_mtx);
}

ModbusTCPServer::ModbusTCPServer() : server(MODBUS_TCP_PORT) {
    memset(coils, 0, sizeof(coils));
    memset(registers, 0, sizeof(registers));
    memset(client_active, 0, sizeof(client_active));
    memset(session_ids, 0, sizeof(session_ids));
}

bool ModbusTCPServer::start() {
    // Establish the listening socket. On the W5500 the loop's handle() path also
    // re-arms LISTEN via server.available() each scan, so this self-heals if the
    // socket subsystem was briefly not ready at boot.
    for (int i = 0; i < 3; i++) { server.begin(); delay(20); }  // 3x = boot retry robustness (begin() is best-effort); available() maintains one listener thereafter
#ifdef ENABLE_ETHERNET
    eth_dump_sockets();
#endif
    Serial.printf("[Modbus] TCP server started on port %d\n", MODBUS_TCP_PORT);
    return true;
}

// Keep the W5500 socket LISTEN pool topped up. Each server.begin() turns one
// CLOSED hardware socket back into LISTEN, so calling this periodically recycles
// sockets left by closed/dropped connections and keeps several listeners ready.
void ModbusTCPServer::topup_listeners() {
#ifdef ENABLE_ETHERNET
    // Share the W5500's 8 hardware sockets with the HTTP server: keep adding
    // Modbus listeners only while the total LISTEN pool leaves room for HTTP
    // listeners and active connections.
    if (eth_listen_count(MODBUS_TCP_PORT) < 2 && eth_listen_count() < 4) server.begin();
#else
    server.begin();
#endif
}

namespace {
uint16_t be16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
void close_owned_client(NetClient& client) {
#ifdef ENABLE_ETHERNET
    // connected() is false for CLOSE_WAIT with an empty RX buffer, but that
    // hardware socket still needs DISCON/CLOSE before returning to the pool.
    // A socket reaped and recycled as LISTEN is no longer ours to stop.
    uint8_t state = client.status();
    if (state != SnSR::CLOSED && state != SnSR::LISTEN) client.stop();
#else
    client.stop();
#endif
}
}

void ModbusTCPServer::stop() {
    for (int i = 0; i < MODBUS_TCP_MAX_CLIENTS; i++) {
        if (client_active[i]) close_owned_client(clients[i]);
        clients[i] = NetClient();
        client_active[i] = false;
        receive[i] = ReceiveState{};
    }
}

void ModbusTCPServer::handle() {
    // Each accepted connection has its own bounded ADU and absolute assembly
    // deadline. Service existing clients first, with a fixed budget per client.
    for (uint8_t i = 0; i < MODBUS_TCP_MAX_CLIENTS; i++)
        if (client_active[i]) process_client(i);
    NetClient client = server.accept(); // both pinned network libraries transfer ownership
    if (!client) return;
    for (uint8_t i = 0; i < MODBUS_TCP_MAX_CLIENTS; i++) {
        if (client_active[i]) continue;
        clients[i] = client;
        client_active[i] = true;
        receive[i] = ReceiveState{};
        receive[i].last_activity = millis();
        char ip[16];
        client.remoteIP().toString().toCharArray(ip, sizeof(ip));
        snprintf(receive[i].source, sizeof(receive[i].source), "tcp/%s:%u", ip, client.remotePort());
        process_client(i);
        return;
    }
    client.stop(); // bounded connection pool; caller may reconnect
}

void ModbusTCPServer::process_client(uint8_t idx) {
    NetClient& client = clients[idx];
    ReceiveState& rx = receive[idx];
    auto release = [&]() {
        // A reaped W5500 socket may already be a listener. Never stop that
        // recycled listener using an obsolete connection object.
        close_owned_client(client);
        client = NetClient(); client_active[idx] = false; rx = ReceiveState{};
    };
    if (!client.connected()) { release(); return; }
    uint32_t now = millis();
    if ((rx.used && uint32_t(now - rx.started) >= FRAME_TIMEOUT_MS) ||
        uint32_t(now - rx.last_activity) >= IDLE_TIMEOUT_MS) { release(); return; }
    // At most two maximum ADUs per visit, including any already buffered tail.
    // Read only bytes reported ready; readBytes must never wait for a packet.
    size_t budget = 2 * MAX_ADU;
    while (budget && client.available() > 0) {
        uint16_t target = 6;
        if (rx.used >= 6) {
            uint16_t length = be16(rx.data + 4);
            if (be16(rx.data + 2) != 0 || length < 2 || length > MAX_ADU - 6) {
                release(); return; // no byte-scanning a corrupt TCP envelope
            }
            target = 6 + length;
        }
        size_t want = target - rx.used;
        size_t available = client.available();
        if (want > available) want = available;
        if (want > budget) want = budget;
        if (!rx.used) rx.started = millis();
        size_t got = client.readBytes(rx.data + rx.used, want);
        if (!got || got > want) { release(); return; }
        rx.used += got; budget -= got; rx.last_activity = millis();
        if (rx.used < 6) continue;
        uint16_t length = be16(rx.data + 4);
        if (be16(rx.data + 2) != 0 || length < 2 || length > MAX_ADU - 6) {
            release(); return;
        }
        if (rx.used != 6 + length) continue;
        uint8_t response[MAX_ADU]; uint16_t response_len = 0;
        // Unit 1 is the teaching PLC. 255 is the direct TCP device alias;
        // other units cannot accidentally address this controller.
        uint8_t unit = rx.data[6] == 255 ? 255 : 1;
        if (handle_frame(rx.data, rx.used, response, sizeof(response), response_len, rx.source, unit)) {
            // Do not replay a partly transmitted write reply (the write already
            // committed). A failed/short send terminates this connection.
            if (client.write(response, response_len) != response_len) { release(); return; }
        }
        rx.used = 0;
    }
}

bool ModbusTCPServer::handle_frame(const uint8_t* request, uint16_t req_len,
        uint8_t* response, size_t capacity, uint16_t& resp_len,
        const char* source, uint8_t unit) {
    resp_len = 0;
    if (!request || !response || req_len < 8 || req_len > MAX_ADU ||
        be16(request + 2) != 0 || be16(request + 4) != req_len - 6 ||
        request[6] != unit) return false;
    const uint8_t* pdu = request + 7;
    uint16_t pdu_len = req_len - 7;
    uint8_t fc = pdu[0], error = 0;
    uint16_t required = 9; // exception response
    bool multiple = fc == 15 || fc == 16;
    bool supported = fc == 1 || fc == 3 || fc == 5 || fc == 6 || multiple;
    if (!supported) error = 1; // illegal function
    else if ((!multiple && pdu_len != 5) || (multiple && pdu_len < 6)) error = 3;
    else {
        uint16_t address = be16(pdu + 1), count = be16(pdu + 3);
        uint16_t limit = (fc == 1 || fc == 5 || fc == 15) ? 512 : 256;
        if (fc == 5 || fc == 6) {
            if (fc == 5 && count != 0 && count != 0xff00) error = 3;
            else if (address >= limit) error = 2;
            required = 12;
        } else {
            uint16_t max_count = fc == 1 ? 2000 : fc == 3 ? 125 : fc == 15 ? 1968 : 123;
            if (!count || count > max_count) error = 3;
            else if (multiple && (pdu[5] != (fc == 15 ? (count + 7) / 8 : count * 2) ||
                                  pdu_len != 6 + pdu[5])) error = 3;
            else if (address >= limit || count > limit - address) error = 2;
            required = multiple ? 12 : 9 + (fc == 1 ? (count + 7) / 8 : count * 2);
        }
    }
    if (error) required = 9;
    // Capacity is checked BEFORE any read of process data, mutation or audit.
    if (capacity < required) return false;
    memcpy(response, request, 7);
    response[7] = fc;
    if (error) {
        response[4] = 0; response[5] = 3;
        response[7] = fc | 0x80; response[8] = error; resp_len = 9;
        return true;
    }
    struct ImageLock { ImageLock() { plc_image_lock(); } ~ImageLock() { plc_image_unlock(); } } lock;
    uint16_t data_len = 0;
    switch (fc) {
        case 1: fc_read_coils(pdu, response + 8, data_len); break;
        case 3: fc_read_registers(pdu, response + 8, data_len); break;
        case 5: fc_write_single_coil(pdu, response + 8, data_len, source); break;
        case 6: fc_write_single_register(pdu, response + 8, data_len, source); break;
        case 15: fc_write_multiple_coils(pdu, response + 8, data_len, source); break;
        case 16: fc_write_multiple_registers(pdu, response + 8, data_len, source); break;
    }
    response[4] = (data_len + 2) >> 8;
    response[5] = (data_len + 2) & 255;
    resp_len = data_len + 8;
    return true;
}

bool ModbusTCPServer::fc_read_coils(const uint8_t* req, uint8_t* resp, uint16_t& resp_len) {
    uint16_t start_addr = (req[1] << 8) | req[2];
    uint16_t quantity = (req[3] << 8) | req[4];

    if (quantity > 2000 || start_addr + quantity > 512) return false;

    uint8_t byte_count = (quantity + 7) / 8;
    resp[0] = byte_count;
    memset(resp + 1, 0, byte_count); // also zero unused bits in the final byte

    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t addr = start_addr + i;
        bool state = (coils[addr / 16] >> (addr % 16)) & 1;
        if (state) {
            resp[1 + (i / 8)] |= (1 << (i % 8));
        }
    }

    resp_len = 1 + byte_count;
    return true;
}

bool ModbusTCPServer::fc_read_registers(const uint8_t* req, uint8_t* resp, uint16_t& resp_len) {
    uint16_t start_addr = (req[1] << 8) | req[2];
    uint16_t quantity = (req[3] << 8) | req[4];

    if (quantity > 125 || start_addr + quantity > 256) return false;

    resp[0] = quantity * 2;

    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t val = registers[start_addr + i];
        resp[1 + i * 2] = val >> 8;
        resp[1 + i * 2 + 1] = val & 0xFF;
    }

    resp_len = 1 + quantity * 2;
    return true;
}

bool ModbusTCPServer::fc_write_single_coil(const uint8_t* req, uint8_t* resp, uint16_t& resp_len, const char* source) {
    uint16_t coil_addr = (req[1] << 8) | req[2];
    uint16_t value = (req[3] << 8) | req[4];

    if (coil_addr >= 512) return false;

#ifndef ENABLE_CWE_DEMOS
    // CWE-306 fixed: an unauthenticated Modbus peer cannot actuate outputs. Writes are
    // accepted only when an operator has placed the controller in the supervised REMOTE
    // position (the key-switch control); otherwise the write is refused with no effect.
    if (plc_mode() != PLC_REMOTE) {
        plc_log(SRC_MODBUS, source, "REFUSED coil %u write - not in REMOTE mode (CWE-306)", coil_addr);
        resp[0]=req[1]; resp[1]=req[2]; resp[2]=req[3]; resp[3]=req[4]; resp_len=4;
        return true;
    }
#endif
    // CWE-306: Missing authentication for critical function
    // E-stop coil (0) can be written by anyone without permission check
    if (value == 0xFF00) {
        coils[coil_addr / 16] |= (1 << (coil_addr % 16));
    } else {
        coils[coil_addr / 16] &= ~(1 << (coil_addr % 16));
    }

    // Faithful PLC behavior: the write lands in the coil image and is applied to
    // the physical output at the next scan boundary (plc_core_scan), where forcing
    // and RUN/PROGRAM mode also apply. Record it with its source: CWE-306 checks no
    // authorization, so this audit trail is the only evidence of who wrote what.
    {
        const TagDef* t = plc_tag_for(TK_COIL, coil_addr);
        plc_log(SRC_MODBUS, source, "WRITE coil %u (%s) = %d", coil_addr,
                t ? t->name : "coil", value == 0xFF00 ? 1 : 0);
    }

    // Echo back (standard Modbus response)
    resp[0] = req[1];
    resp[1] = req[2];
    resp[2] = req[3];
    resp[3] = req[4];
    resp_len = 4;
    return true;
}

bool ModbusTCPServer::fc_write_single_register(const uint8_t* req, uint8_t* resp, uint16_t& resp_len, const char* source) {
    uint16_t reg_addr = (req[1] << 8) | req[2];
    uint16_t value = (req[3] << 8) | req[4];

    if (reg_addr >= 256) return false;

#ifndef ENABLE_CWE_DEMOS
    if (plc_mode() != PLC_REMOTE) {                 // CWE-306 fixed (see fc_write_single_coil)
        plc_log(SRC_MODBUS, source, "REFUSED reg %u write - not in REMOTE mode (CWE-306)", reg_addr);
        resp[0]=req[1]; resp[1]=req[2]; resp[2]=req[3]; resp[3]=req[4]; resp_len=4;
        return true;
    }
#endif
    registers[reg_addr] = value;
    {
        const TagDef* t = plc_tag_for(TK_REG, reg_addr);
        plc_log(SRC_MODBUS, source, "WRITE reg %u (%s) = %u", reg_addr,
                t ? t->name : "reg", value);
    }

    resp[0] = req[1];
    resp[1] = req[2];
    resp[2] = req[3];
    resp[3] = req[4];
    resp_len = 4;
    return true;
}

bool ModbusTCPServer::fc_write_multiple_coils(const uint8_t* req, uint8_t* resp, uint16_t& resp_len, const char* source) {
    uint16_t start_addr = (req[1] << 8) | req[2];
    uint16_t quantity = (req[3] << 8) | req[4];
    uint8_t byte_count = req[5];

    if (quantity > 1968 || start_addr + quantity > 512) return false;

#ifndef ENABLE_CWE_DEMOS
    if (plc_mode() != PLC_REMOTE) {                 // CWE-306 fixed (see fc_write_single_coil)
        plc_log(SRC_MODBUS, source, "REFUSED coils %u count %u write - not in REMOTE mode (CWE-306)", start_addr, quantity);
        resp[0]=req[1]; resp[1]=req[2]; resp[2]=req[3]; resp[3]=req[4]; resp_len=4;
        return true;
    }
#endif
    for (uint16_t i = 0; i < quantity; i++) {
        bool state = (req[6 + (i / 8)] >> (i % 8)) & 1;
        if (state) {
            coils[(start_addr + i) / 16] |= (1 << ((start_addr + i) % 16));
        } else {
            coils[(start_addr + i) / 16] &= ~(1 << ((start_addr + i) % 16));
        }
    }

    plc_log(SRC_MODBUS, source, "WRITE coils %u count %u", start_addr, quantity);

    resp[0] = req[1];
    resp[1] = req[2];
    resp[2] = req[3];
    resp[3] = req[4];
    resp_len = 4;
    return true;
}

bool ModbusTCPServer::fc_write_multiple_registers(const uint8_t* req, uint8_t* resp, uint16_t& resp_len, const char* source) {
    uint16_t start_addr = (req[1] << 8) | req[2];
    uint16_t quantity = (req[3] << 8) | req[4];
    uint8_t byte_count = req[5];

    if (quantity > 123 || start_addr + quantity > 256) return false;

#ifndef ENABLE_CWE_DEMOS
    if (plc_mode() != PLC_REMOTE) {                 // CWE-306 fixed (see fc_write_single_coil)
        plc_log(SRC_MODBUS, source, "REFUSED regs %u count %u write - not in REMOTE mode (CWE-306)", start_addr, quantity);
        resp[0]=req[1]; resp[1]=req[2]; resp[2]=req[3]; resp[3]=req[4]; resp_len=4;
        return true;
    }
#endif
    for (uint16_t i = 0; i < quantity; i++) {
        registers[start_addr + i] = (req[6 + i * 2] << 8) | req[6 + i * 2 + 1];
    }

    plc_log(SRC_MODBUS, source, "WRITE regs %u count %u", start_addr, quantity);

    resp[0] = req[1];
    resp[1] = req[2];
    resp[2] = req[3];
    resp[3] = req[4];
    resp_len = 4;
    return true;
}

void ModbusTCPServer::set_coil(uint16_t addr, bool state) {
    if (addr < 512) {
        plc_image_lock();
        if (state) coils[addr / 16] |= (1 << (addr % 16));
        else       coils[addr / 16] &= ~(1 << (addr % 16));
        plc_image_unlock();
    }
}

bool ModbusTCPServer::get_coil(uint16_t addr) {
    if (addr >= 512) return false;
    plc_image_lock();
    bool v = (coils[addr / 16] >> (addr % 16)) & 1;
    plc_image_unlock();
    return v;
}

void ModbusTCPServer::set_register(uint16_t addr, uint16_t val) {
    if (addr < 256) {
        plc_image_lock();
        registers[addr] = val;
        plc_image_unlock();
    }
}

uint16_t ModbusTCPServer::get_register(uint16_t addr) {
    if (addr >= 256) return 0;
    plc_image_lock();
    uint16_t v = registers[addr];
    plc_image_unlock();
    return v;
}

bool ModbusTCPServer::authenticate(const char* user, const char* pass) {
    // CWE-798: Hardcoded credentials (no real auth)
    // Just check if password matches hardcoded value
    return strcmp(pass, MODBUS_HARDCODED_PASS) == 0;
}

// CWE-330: Predictable PRNG for session token
uint32_t ModbusTCPServer::generate_session_id(uint8_t client_idx) {
    // Seeded only from millis() - highly predictable!
    // Student can observe millis() value and predict next token
    uint32_t seed = esp_timer_get_time() % 1000;
    return (seed << 16) | client_idx;
}
