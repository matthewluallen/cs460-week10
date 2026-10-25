#include "device_acquisition.h"
#include "acquisition_config.h"
#include "process_bindings.h"
#include <Arduino.h>
#include <Wire.h>
#include <M5Unified.h>
#include <bme68x.h>

namespace {
acquisition::Engine engine;
class Transport : public devices::Bus {
    bool internal;
public:
    explicit Transport(bool i):internal(i){}
    bool ack(uint8_t a) override {
        if(internal)return m5::In_I2C.scanID(a,100000);
        Wire1.beginTransmission(a);return Wire1.endTransmission()==0;
    }
    bool write(uint8_t a,const uint8_t* p,size_t n) override {
        if(internal)return false; // This layer only reads module inputs.
        Wire1.beginTransmission(a);
        return Wire1.write(p,n)==n && Wire1.endTransmission()==0;
    }
    bool read(uint8_t a,uint8_t* p,size_t n) override {
        if(internal || Wire1.requestFrom(a,n,true)!=n)return false;
        for(size_t i=0;i<n;++i)p[i]=Wire1.read();return true;
    }
    bool reg16(uint8_t a,uint16_t r,uint8_t* p,size_t n) override {
        if(internal)return false;
        Wire1.beginTransmission(a);Wire1.write(uint8_t(r>>8));Wire1.write(uint8_t(r));
        return Wire1.endTransmission(false)==0 && read(a,p,n);
    }
    bool reg(uint8_t a,uint8_t r,uint8_t* p,size_t n) override {
        if(internal)return m5::In_I2C.readRegister(a,r,p,n,100000);
        Wire1.beginTransmission(a);Wire1.write(r);
        return Wire1.endTransmission(false)==0 && read(a,p,n);
    }
} external(false),internal(true);
struct Env {
    bme68x_dev dev{};
    bme68x_conf conf{};
    uint8_t address=0, field[17]{};
    bool cached=false;
    uint32_t began=0;
    bool allowed() const {return millis()-began<30;}
} env[acquisition::Engine::Capacity];
BME68X_INTF_RET_TYPE env_read_reg(uint8_t r,uint8_t* p,uint32_t n,void* ctx) {
    auto& e=*static_cast<Env*>(ctx);
    if(!e.allowed())return -1;
    // The header check captures exactly the field Bosch subsequently compensates.
    // This avoids its retry/sleep loop when no new conversion is available.
    if(e.cached && r==0x1d && n==17){memcpy(p,e.field,17);return 0;}
    return external.reg(e.address,r,p,n)?0:-1;
}
BME68X_INTF_RET_TYPE env_write_reg(uint8_t r,const uint8_t* p,uint32_t n,void* ctx) {
    auto& e=*static_cast<Env*>(ctx);if(n>63 || !e.allowed())return -1;
    uint8_t data[64];data[0]=r;memcpy(data+1,p,n);
    return external.write(e.address,data,n+1)?0:-1;
}
void env_delay(uint32_t us,void* ctx) {
    auto& e=*static_cast<Env*>(ctx);
    if(e.allowed() && us<=10000)delayMicroseconds(us);
}
class Hardware : public acquisition::Backend {
public:
    devices::Bus& bus(bool i) override {return i?internal:external;}
    uint32_t clock_ms() override {return millis();}
    bool env_init(size_t i,const devices::Device& d) override {
        auto& e=env[i];e=Env{};e.address=d.address;e.began=millis();
        e.dev.intf=BME68X_I2C_INTF;e.dev.intf_ptr=&e;e.dev.read=env_read_reg;
        e.dev.write=env_write_reg;e.dev.delay_us=env_delay;e.dev.amb_temp=25;
        if(bme68x_init(&e.dev)!=BME68X_OK)return false;
        e.conf.os_hum=BME68X_OS_2X;e.conf.os_temp=BME68X_OS_8X;e.conf.os_pres=BME68X_OS_4X;
        e.conf.filter=BME68X_FILTER_SIZE_3;e.conf.odr=BME68X_ODR_NONE;
        bme68x_heatr_conf heater{};heater.enable=BME68X_ENABLE;heater.heatr_temp=acquisition::EnvHeaterC;heater.heatr_dur=acquisition::EnvHeaterMs;
        return bme68x_set_conf(&e.conf,&e.dev)==BME68X_OK &&
            bme68x_set_heatr_conf(BME68X_FORCED_MODE,&heater,&e.dev)==BME68X_OK;
    }
    bool env_start(size_t i,uint32_t& wait) override {
        auto& e=env[i];e.began=millis();uint8_t mode=255;
        if(bme68x_get_op_mode(&mode,&e.dev)!=BME68X_OK || mode!=BME68X_SLEEP_MODE)return false;
        // Discard any unconsumed field before this explicitly owned conversion.
        if(!external.reg(e.address,0x1d,e.field,sizeof(e.field)))return false;
        if(bme68x_set_op_mode(BME68X_FORCED_MODE,&e.dev)!=BME68X_OK)return false;
        wait=(bme68x_get_meas_dur(BME68X_FORCED_MODE,&e.conf,&e.dev)+999)/1000+acquisition::EnvHeaterMs+5;
        return true;
    }
    int env_read(size_t i,acquisition::Values& v) override {
        auto& e=env[i];e.began=millis();
        if(!external.reg(e.address,0x1d,e.field,sizeof(e.field)))return -1;
        v.env_status=e.field[0];v.env_index=e.field[1];
        // Bosch's new-data and measuring flags determine readiness. The
        // sub-measurement index is diagnostic, not our sequence counter.
        if(!(e.field[0]&BME68X_NEW_DATA_MSK) || (e.field[0]&0x60))return 0;
        bme68x_data d{};uint8_t n=0;e.cached=true;
        int8_t rc=bme68x_get_data(BME68X_FORCED_MODE,&d,&n,&e.dev);e.cached=false;
        if(rc!=BME68X_OK || n!=1 || !(d.status&BME68X_NEW_DATA_MSK))return -1;
        if(!isfinite(d.temperature)||!isfinite(d.humidity)||!isfinite(d.pressure) ||
           d.temperature < -40 || d.temperature > 85 || d.humidity < 0 || d.humidity > 100 ||
           d.pressure<30000 || d.pressure>110000)return -1;
        v.temp_c=d.temperature;v.rh_pct=d.humidity;v.pressure_pa=d.pressure;
        v.gas_valid=(d.status&(BME68X_GASM_VALID_MSK|BME68X_HEAT_STAB_MSK))==
            (BME68X_GASM_VALID_MSK|BME68X_HEAT_STAB_MSK) && isfinite(d.gas_resistance) && d.gas_resistance>0;
        v.gas_ohm=v.gas_valid?d.gas_resistance:0;
        v.env_sample_status=d.status;v.env_sample_index=d.meas_index;
        v.env_gas_index=d.gas_index;v.env_res_heat=d.res_heat;v.env_idac=d.idac;v.env_gas_wait=d.gas_wait;
        return 1;
    }
} hardware;
}
acquisition::Engine& device_acquisition_state(){return engine;}
void device_acquisition_bind(const devices::Inventory& inventory){plc_bindings_invalidate();engine.bind(inventory,millis());}
void device_acquisition_tick(){
    // A single ready slot per main loop starves FIFO sensors when other PLC
    // work lengthens that loop. Drain a bounded batch, closing each route.
    // The budget is soft: the last bounded I2C stage may finish after it.
    uint32_t batch=micros();
    for(unsigned n=0;n<8 && micros()-batch<16000;++n){
        uint32_t start=micros();engine.tick(hardware);uint32_t elapsed=micros()-start;
        if(elapsed>engine.max_step_us)engine.max_step_us=elapsed;
    }
    uint32_t elapsed=micros()-batch;
    if(elapsed>engine.max_batch_us)engine.max_batch_us=elapsed;
}
bool device_acquisition_pause(){
    plc_bindings_invalidate();
    engine.enabled=false;bool ok=true;
    for(size_t i=0;i<engine.count;++i){const auto& s=engine.slots[i];if(s.kind!=acquisition::Kind::Heart && s.kind!=acquisition::Kind::Tof)continue;
        auto& b=hardware.bus(s.device.bus);const auto& d=s.device;
        bool selected=!d.mux || devices::Inventory::select(b,d.mux,1u<<d.channel);
        const uint8_t heart_shut[]={6,0x80},tof_shut[]={0,0x87,0x80};
        bool stopped=selected&&(s.kind==acquisition::Kind::Tof?b.write(d.address,tof_shut,3):b.write(d.address,heart_shut,2));
        bool closed=!d.mux || devices::Inventory::select(b,d.mux,0);
        ok=ok&&stopped&&closed;
    }
    return ok;
}
