#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include <cstring>
#include "openplc_runtime.h"

OpenPLCRuntime openplc_runtime;

OpenPLCRuntime::OpenPLCRuntime() : running(false), last_cycle_time(0), cycle_count(0) {
    memset(coils, 0, sizeof(coils));
    memset(registers, 0, sizeof(registers));
}

bool OpenPLCRuntime::init() {
    Serial.println("[OpenPLC] Runtime initialized");
    return true;
}

void OpenPLCRuntime::start() {
    running = true;
    cycle_count = 0;
    Serial.println("[OpenPLC] Runtime started");
}

void OpenPLCRuntime::stop() {
    running = false;
    Serial.println("[OpenPLC] Runtime stopped");
}

void OpenPLCRuntime::scan_cycle() {
    if (!running) return;

    uint32_t start = millis();

    // Compatibility image/timing shim only. Applications are loaded through
    // the project-v1 VM loader; no process behavior is compiled here.

    last_cycle_time = millis() - start;
    cycle_count++;
}

void OpenPLCRuntime::set_coil(uint16_t addr, bool state) {
    if (addr < 512) {
        if (state) {
            coils[addr / 16] |= (1 << (addr % 16));
        } else {
            coils[addr / 16] &= ~(1 << (addr % 16));
        }
    }
}

bool OpenPLCRuntime::get_coil(uint16_t addr) {
    if (addr >= 512) return false;
    return (coils[addr / 16] >> (addr % 16)) & 1;
}

void OpenPLCRuntime::set_register(uint16_t addr, uint16_t val) {
    if (addr < 512) {
        registers[addr] = val;
    }
}

uint16_t OpenPLCRuntime::get_register(uint16_t addr) {
    if (addr >= 512) return 0;
    return registers[addr];
}

bool OpenPLCRuntime::load_program(const uint8_t* bytecode, size_t len) {
    (void)bytecode; (void)len;
    Serial.println("[OpenPLC] legacy bytecode loader unsupported; deploy a TBD-PROJECT v1 project");
    return false;
}

bool OpenPLCRuntime::start_program() {
    Serial.println("[OpenPLC] legacy start unsupported; use project loading and explicit mode RUN");
    return false;
}

void OpenPLCRuntime::stop_program() {
    stop();
}

uint32_t OpenPLCRuntime::get_cycle_time_ms() {
    return last_cycle_time;
}

bool OpenPLCRuntime::is_running() {
    return running;
}
