#include "m5stamplc_io.h"
#include <M5StamPLC.h>
#include "plc_core.h"

// I/O layer wrapping M5Stack's official M5StamPLC library (MIT) so relays,
// digital inputs and sensors use the REAL StamPLC hardware map (AW9523B, INA226,
// LM75, RTC) instead of the previous guessed pinout.
M5StampLCIO io;
M5StampLCIO::M5StampLCIO() : relay_state(0), di_state(0) {}

bool M5StampLCIO::init() {
    M5StamPLC.begin();
    relay_state = 0;
    return true;
}
void M5StampLCIO::relay_set(uint8_t idx, bool state) {
    if (idx > 3) return;
    bool prev = relay_state & (1 << idx);
    M5StamPLC.writePlcRelay(idx, state);
    if (prev != state) plc_bus_log(0x59, 'W', state ? "AW9523 relay ON" : "AW9523 relay OFF", idx, true);
    if (state) relay_state |= (1 << idx); else relay_state &= ~(1 << idx);
}
bool M5StampLCIO::relay_get(uint8_t idx) {
    if (idx > 3) return false;
    return M5StamPLC.readPlcRelay(idx);
}
bool M5StampLCIO::relay_write_mask(uint8_t values) {
    uint8_t latch=0;
    if(!m5::In_I2C.readRegister(0x59,0x02,&latch,1,400000))return false;
    const uint8_t next=(latch&0xf0)|(values&0x0f);
    // Always issue the write, including OFF retries. ACK is not actuation proof.
    if(!m5::In_I2C.writeRegister(0x59,0x02,&next,1,400000))return false;
    for(unsigned i=0;i<4;++i)if((relay_state^next)&(1<<i))
        plc_bus_log(0x59,'W',(next&(1<<i))?"AW9523 relay ON":"AW9523 relay OFF",i,true);
    relay_state=next&0x0f;
    return true;
}
bool M5StampLCIO::relay_snapshot(uint8_t& values) {
    uint8_t port=0;
    if(!m5::In_I2C.readRegister(0x59,0x00,&port,1,400000))return false;
    values=port&0x0f;
    return true;
}
bool M5StampLCIO::di_read(uint8_t idx) {
    if (idx > 7) return false;
    return M5StamPLC.readPlcInput(idx);
}
bool M5StampLCIO::di_snapshot(uint8_t& values) {
    // Same pinned M5StamPLC mapping: channels0..3=P0.4..7,4..7=P1.4..7.
    // Use success-bearing API: readRegister8() silently substitutes zero on error.
    uint8_t port0=0,port1=0;
    if(!m5::In_I2C.readRegister(0x59,0x00,&port0,1,400000) ||
       !m5::In_I2C.readRegister(0x59,0x01,&port1,1,400000))return false;
    values=(port0>>4)|(port1&0xf0);
    return true;
}
void  M5StampLCIO::refresh()      { M5StamPLC.update(); }
// These library getters read LM75/INA226 registers directly. General update()
// polls M5Unified devices/buttons and is neither needed nor a sensor refresh.
float M5StampLCIO::temp_read()    { return M5StamPLC.getTemp(); }
float M5StampLCIO::voltage_read() { return M5StamPLC.getPowerVoltage(); }
float M5StampLCIO::current_read() { return M5StamPLC.getIoSocketOutputCurrent(); }
void M5StampLCIO::lcd_init()  {}
void M5StampLCIO::lcd_print(uint16_t x, uint16_t y, const char* text, uint16_t color) {}
void M5StampLCIO::lcd_clear() {}
void M5StampLCIO::update_display() {}
