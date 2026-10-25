#ifndef M5STAMPLC_IO_H
#define M5STAMPLC_IO_H
#include <Arduino.h>

#include <Wire.h>
#include "config.h"

// I/O abstraction for M5 StampLC
// Handles relays, digital inputs, sensors (LM75, INA226)

class M5StampLCIO {
public:
    M5StampLCIO();
    bool init();

    // Relay control (0-3)
    void relay_set(uint8_t idx, bool state);
    bool relay_get(uint8_t idx);
    // P0.0..3 group update; preserve unrelated latch bits. Bus status, not contacts.
    bool relay_write_mask(uint8_t values);
    bool relay_snapshot(uint8_t& values);

    // Digital input read (0-7)
    bool di_read(uint8_t idx);
    // Two sequential AW9523 input-register reads; unchanged destination on failure.
    // Not an atomic electrical snapshot across ports.
    bool di_snapshot(uint8_t& values);

    // Sensor reads
    float temp_read();          // LM75 temperature in C
    float voltage_read();       // INA226 bus voltage in V
    float current_read();       // INA226 current in A
    void  refresh();            // explicit M5Unified device/button update; sensor getters read directly

    // Display
    void lcd_init();
    void lcd_print(uint16_t x, uint16_t y, const char* text, uint16_t color = 0xFFFF);
    void lcd_clear();

    // Status update (called from main loop)
    void update_display();

private:
    // I/O expander state caching
    uint8_t relay_state;
    uint8_t di_state;

    // I2C communication
    bool expander_write(uint8_t addr, uint8_t reg, uint8_t val);
    uint8_t expander_read(uint8_t addr, uint8_t reg);
    uint16_t expander_read16(uint8_t addr, uint8_t reg);
};

extern M5StampLCIO io;

#endif // M5STAMPLC_IO_H
