#include "io_expansion.h"
#include "stamplc_io_module.h"   // vendored M5StamPLC_IO driver

static M5StamPLC_IO s_io;
static bool s_present = false;

bool ioexp_begin() {
    // begin(0) auto-scans 0x20-0x2F on m5::In_I2C (internal StamPLC bus 13/15),
    // which M5StamPLC.begin() has already initialized.
    s_present = s_io.begin();
    if (s_present) {
        s_io.setAllRelays(false);
        s_io.setPWMMode(false);
    }
    return s_present;
}

bool ioexp_present() { return s_present; }

bool ioexp_read(int16_t* v1, int32_t* i1, int16_t* v2, int32_t* i2) {
    if (!s_present || !v1 || !i1 || !v2 || !i2) return false;
    uint8_t p[12],status=0xff;
    if (!m5::In_I2C.readRegister(s_io.getCurrentAddress(),M5StamPLC_IO::REG_SYSTEM_STATUS,&status,1,100000)||status)return false;
    if (!m5::In_I2C.readRegister(s_io.getCurrentAddress(),0,p,sizeof(p),100000))return false;
    const auto le32=[](const uint8_t* b){return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;};
    *v1=int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);*i1=int32_t(le32(p+2));
    *v2=int16_t(uint16_t(p[6])|uint16_t(p[7])<<8);*i2=int32_t(le32(p+8));
    return true;
}

void ioexp_set_mech_relay(bool on) {
    if (s_present) s_io.setRelayState(M5StamPLC_IO::BIT_RELAY_TRIG, on);
}

void ioexp_set_ssr(uint8_t idx, bool on) {
    if (!s_present || idx > 1) return;
    s_io.setRelayState(idx == 0 ? M5StamPLC_IO::BIT_EX_CTR_1 : M5StamPLC_IO::BIT_EX_CTR_2, on);
}

bool ioexp_output_readback(uint8_t* bits) {
    if (!s_present || !bits) return false;
    uint8_t value = 0;
    if (!m5::In_I2C.readRegister(s_io.getCurrentAddress(), M5StamPLC_IO::REG_IO_CONTROL,
                                 &value, 1, 400000)) return false;
    *bits = value & 0x13;
    return true;
}

uint8_t ioexp_addr() { return s_present ? s_io.getCurrentAddress() : 0; }
uint8_t ioexp_fw()   { return s_present ? s_io.getFirmwareVersion() : 0; }
bool ioexp_identity(uint8_t* address,uint8_t* revision) {
    if(!s_present||!address||!revision)return false;
    uint8_t status=0xff,version=0xff,reported_address=0;
    const uint8_t addr=s_io.getCurrentAddress();
    if(!m5::In_I2C.readRegister(addr,M5StamPLC_IO::REG_SYSTEM_STATUS,&status,1,100000)||status||
       !m5::In_I2C.readRegister(addr,M5StamPLC_IO::REG_FIRMWARE_VER,&version,1,100000)||version==0xff||
       !m5::In_I2C.readRegister(addr,M5StamPLC_IO::REG_ADDR_CONFIG,&reported_address,1,100000)||reported_address!=addr)return false;
    *address=addr;*revision=version;return true;
}
