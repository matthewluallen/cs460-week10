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

    // TODO: Call actual OpenPLC interpreter with bytecode
    // For now, this is a stub that demonstrates the latching logic

    // Demo ladder logic: simple latching relay with E-stop
    // DI 0 (set input) -> latch relay 0
    // DI 1 (reset input) -> unlatch relay 0
    // DI 2 (E-stop) -> force all relays OFF

    bool di_set = get_coil(100);    // DI 0 mapped to coil 100
    bool di_reset = get_coil(101);  // DI 1 mapped to coil 101
    bool di_estop = get_coil(102);  // DI 2 (E-stop) mapped to coil 102

    static bool relay_state = false;

    if (di_estop) {
        // E-stop: force all relays OFF (CWE-306: no auth check)
        relay_state = false;
        set_coil(0, false);
        set_coil(1, false);
    } else {
        // Normal latch/unlatch logic
        if (di_set && !relay_state) {
            relay_state = true;
            set_coil(0, true);   // Latched ON
        }
        if (di_reset && relay_state) {
            relay_state = false;
            set_coil(0, false);  // Latched OFF
        }
    }

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
    // TODO: Parse and load OpenPLC bytecode
    Serial.printf("[OpenPLC] Loading program (%u bytes)\n", len);
    return true;
}

bool OpenPLCRuntime::start_program() {
    start();
    return true;
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
