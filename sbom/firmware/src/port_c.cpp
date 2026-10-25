#include "port_c.h"
#include <HardwareSerial.h>
#include <Preferences.h>
#include <cstring>

namespace {
constexpr int TX_PIN = 5, RX_PIN = 4;
HardwareSerial radio(2);  // UART1 belongs exclusively to PWR-485.
PortCProfile active = PortCProfile::Disabled, saved = PortCProfile::Disabled;
bool started = false, uart_ready = false;
uint32_t baud = 115200, saved_baud = 115200;
uint32_t tx_bytes = 0, rx_bytes = 0;

const char* name(PortCProfile p) {
    switch (p) {
        case PortCProfile::HX711: return "hx711";
        case PortCProfile::H2Uart: return "h2-uart";
        default: return "disabled";
    }
}
bool parse_profile(const char* text, PortCProfile& out) {
    if (!text) return false;
    if (!strcmp(text, "disabled")) out = PortCProfile::Disabled;
    else if (!strcmp(text, "hx711")) out = PortCProfile::HX711;
    else if (!strcmp(text, "h2-uart")) out = PortCProfile::H2Uart;
    else return false;
    return true;
}
bool valid_baud(uint32_t value) {
    return value == 9600 || value == 19200 || value == 38400 || value == 57600 ||
           value == 115200 || value == 230400 || value == 460800;
}
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
void error(const char* reason) {
    Serial.printf("{\"portc\":\"error\",\"reason\":\"%s\"}\n", reason);
}
}

void port_c_begin() {
    if (started) return;
    started = true;
    pinMode(TX_PIN, INPUT);
    pinMode(RX_PIN, INPUT);
    Preferences p;
    uint8_t mode = 0;
    bool valid = true;
    if (p.begin("portc", true)) {
        mode = p.getUChar("profile", 0);
        baud = p.getUInt("baud", 115200);
        p.end();
    }
    valid = mode <= uint8_t(PortCProfile::H2Uart) && valid_baud(baud);
    active = valid ? PortCProfile(mode) : PortCProfile::Disabled;
    if (!valid) baud = 115200;
    saved = active; saved_baud = baud;
    if (active == PortCProfile::H2Uart) {
        radio.setRxBufferSize(1024);
        radio.setTxBufferSize(128);
        radio.begin(baud, SERIAL_8N1, RX_PIN, TX_PIN);
        uart_ready = bool(radio);
        if (!uart_ready) {
            radio.end();
            pinMode(TX_PIN, INPUT); pinMode(RX_PIN, INPUT);
            active = PortCProfile::Disabled;
        }
    }
    Serial.printf("[PORTC] active=%s baud=%lu tx=5 rx=4 uart_ready=%d config_valid=%d\n",
                  name(active), (unsigned long)baud, uart_ready, valid);
}

PortCProfile port_c_profile() { return active; }

void port_c_console(const char* operation, const char* argument, bool extra) {
    if (extra) { error("extra arguments"); return; }
    if (!operation || !strcmp(operation, "status")) {
        if (argument) { error("unexpected argument"); return; }
        Serial.printf("{\"portc\":\"status\",\"active\":\"%s\",\"saved\":\"%s\","
                      "\"baud\":%lu,\"saved_baud\":%lu,\"uart_ready\":%s,"
                      "\"rx_bytes\":%lu,\"tx_bytes\":%lu,\"zigbee_verified\":false}\n",
                      name(active), name(saved), (unsigned long)baud, (unsigned long)saved_baud,
                      uart_ready ? "true" : "false", (unsigned long)rx_bytes, (unsigned long)tx_bytes);
        return;
    }
    if (!strcmp(operation, "profile")) {
        PortCProfile requested;
        if (!parse_profile(argument, requested)) { error("profile must be disabled, hx711 or h2-uart"); return; }
        Preferences p;
        if (!p.begin("portc", false)) { error("cannot open profile storage"); return; }
        bool ok = p.putUChar("profile", uint8_t(requested)) == 1 &&
                  p.getUChar("profile", 255) == uint8_t(requested);
        p.end();
        if (!ok) { error("profile persistence failed"); return; }
        saved = requested;
        Serial.printf("{\"portc\":\"profile_saved\",\"saved\":\"%s\",\"reboot_required\":true}\n", name(saved));
        return;
    }
    if (!strcmp(operation, "baud")) {
        uint32_t value = 0;
        if (!argument || !*argument || strlen(argument) > 6) { error("invalid baud"); return; }
        for (const char* c = argument; *c; ++c) {
            if (*c < '0' || *c > '9') { error("invalid baud"); return; }
            value = value * 10 + uint32_t(*c - '0');
        }
        if (!valid_baud(value)) { error("unsupported baud"); return; }
        Preferences p;
        if (!p.begin("portc", false)) { error("cannot open baud storage"); return; }
        bool ok = p.putUInt("baud", value) == sizeof(uint32_t) && p.getUInt("baud", 0) == value;
        p.end();
        if (!ok) { error("baud persistence failed"); return; }
        saved_baud = value;
        Serial.printf("{\"portc\":\"baud_saved\",\"saved_baud\":%lu,\"reboot_required\":true}\n", (unsigned long)value);
        return;
    }
    if (strcmp(operation, "read") && strcmp(operation, "send")) { error("unknown operation"); return; }
    if (!uart_ready) { error("UART is not the active Port C owner"); return; }
    if (!strcmp(operation, "read")) {
        if (argument) { error("unexpected argument"); return; }
        char hex[129]; size_t count = 0;
        const char* digits = "0123456789abcdef";
        while (count < 64 && radio.available() > 0) {
            int c = radio.read(); if (c < 0) break;
            hex[count * 2] = digits[(uint8_t(c) >> 4) & 15];
            hex[count * 2 + 1] = digits[uint8_t(c) & 15]; ++count;
        }
        hex[count * 2] = 0; rx_bytes += count;
        Serial.printf("{\"portc\":\"read\",\"hex\":\"%s\",\"remaining\":%d,\"lossless\":false}\n", hex, radio.available());
        return;
    }
    size_t length = argument ? strlen(argument) : 0;
    if (!length || length > 96 || length % 2) { error("send requires 1..48 hex-encoded bytes"); return; }
    uint8_t data[48];
    for (size_t i = 0; i < length / 2; ++i) {
        int hi = nibble(argument[2 * i]), lo = nibble(argument[2 * i + 1]);
        if (hi < 0 || lo < 0) { error("invalid hex"); return; }
        data[i] = uint8_t((hi << 4) | lo);
    }
    if (radio.availableForWrite() < int(length / 2)) { error("UART transmit queue full"); return; }
    size_t sent = radio.write(data, length / 2); tx_bytes += sent;
    Serial.printf("{\"portc\":\"send\",\"queued_bytes\":%u,\"requested_bytes\":%u,\"peer_acknowledged\":false}\n",
                  unsigned(sent), unsigned(length / 2));
}
