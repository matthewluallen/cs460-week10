#include "device_inventory.h"
#include "device_inventory_core.h"
#include "device_acquisition.h"
#include <Arduino.h>
#include <Wire.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <cstdarg>
#include <cstdio>

namespace {
devices::Inventory inventory;
uint8_t active = 0, saved = 0;
uint16_t active_tof4m=0, saved_tof4m=0;
uint32_t scanned_ms = 0, duration_ms = 0;
bool timed_out = false;
class Transport : public devices::Bus {
    bool internal;
    uint32_t start = 0;
public:
    explicit Transport(bool i) : internal(i) {}
    void begin() { start = millis(); }
    bool allowed() { if (millis() - start <= 2000) return true; timed_out = true; return false; }
    bool ack(uint8_t a) override {
        if (!allowed()) return false;
        if (internal) return m5::In_I2C.scanID(a, 100000);
        Wire1.beginTransmission(a); return Wire1.endTransmission() == 0;
    }
    bool write(uint8_t a, const uint8_t* p, size_t n) override {
        // A final mux deselect is attempted even after the scan deadline.
        if (!allowed() && !(a >= 0x70 && a <= 0x77 && n == 1 && *p == 0)) return false;
        if (internal) return false; // Discovery never writes module configuration.
        Wire1.beginTransmission(a); Wire1.write(p, n);
        bool ok = Wire1.endTransmission() == 0;
        if (ok && a == 0x58 && n == 2) {
            // FreeRTOS delay(10) can wake before ten elapsed milliseconds,
            // depending on tick phase. Measure from command completion.
            uint32_t sent=millis();while(millis()-sent<12)delay(1);
        }
        return ok;
    }
    bool read(uint8_t a, uint8_t* p, size_t n) override {
        // Permit mux readback on the cleanup path too.
        if (!allowed() && !(a >= 0x70 && a <= 0x77 && n == 1)) return false;
        if (internal || Wire1.requestFrom(a, n, true) != n) return false;
        for (size_t i = 0; i < n; ++i) p[i] = Wire1.read();
        return true;
    }
    bool reg16(uint8_t a,uint16_t r,uint8_t* p,size_t n) override {
        if(internal || !allowed())return false;
        Wire1.beginTransmission(a);Wire1.write(uint8_t(r>>8));Wire1.write(uint8_t(r));
        return Wire1.endTransmission(false)==0 && read(a,p,n);
    }
    bool reg(uint8_t a, uint8_t r, uint8_t* p, size_t n) override {
        if (!allowed()) return false;
        if (internal) return m5::In_I2C.readRegister(a, r, p, n, 100000);
        Wire1.beginTransmission(a); Wire1.write(r);
        return Wire1.endTransmission(false) == 0 && read(a, p, n);
    }
} external(false), internal(true);
void error(const char* text) { Serial.printf("{\"devices\":\"error\",\"reason\":\"%s\"}\n", text); }
// The global USB timeout stays at 1 ms for watchdog resilience. Respect queue
// capacity and short writes here instead of dropping the end of large JSON.
class Output {
    uint32_t start = millis();
    bool failed = false;
public:
    void print(const char* format, ...) {
        if (failed) return;
        char buffer[512]; va_list args; va_start(args,format);
        int n=vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
        if(n<0 || n>=int(sizeof(buffer))) { failed=true; return; }
        size_t pos=0;
        while(pos<size_t(n)) {
            if(millis()-start>=750) { failed=true; return; }
            size_t chunk=size_t(n)-pos; if(chunk>32)chunk=32;
            if(Serial.availableForWrite()<int(chunk)) { delay(1); continue; }
            size_t sent=Serial.write(reinterpret_cast<const uint8_t*>(buffer+pos),chunk);
            if(sent>chunk) { failed=true; return; }
            pos+=sent; if(!sent)delay(1);
        }
    }
    void finish() {
        // Leave room for the console prompt without changing global timeout.
        while(Serial.availableForWrite()<64 && millis()-start<750)delay(1);
    }
};
void samples() {
    Output out;auto& e=device_acquisition_state();uint32_t now=millis();
    out.print("{\"devices\":\"samples\",\"schema\":1,\"generation\":%lu,\"epoch\":%lu,\"clock\":\"boot_monotonic_read_completion\","
              "\"enabled\":%s,\"route_ok\":%s,\"overflow\":%s,\"max_step_us\":%lu,\"max_batch_us\":%lu,\"entry_count\":%u,\"entries\":[",
              (unsigned long)e.generation,(unsigned long)e.epoch,e.enabled?"true":"false",e.route_ok?"true":"false",
              e.overflow?"true":"false",(unsigned long)e.max_step_us,(unsigned long)e.max_batch_us,unsigned(e.count));
    for(size_t i=0;i<e.count;++i){const auto& s=e.slots[i];const auto& d=s.device;const auto& v=s.value;
        auto q=s.current(now,e.enabled);
        out.print("%s{\"bus\":\"%s\",\"mux\":%u,\"channel\":%d,\"address\":%u,\"driver\":\"%s\","
                  "\"quality\":\"%s\",\"valid\":%s,\"has_sample\":%s,\"sequence\":%lu,\"read_ms\":%lu,",
                  i?",":"",d.bus?"internal":"porta",d.mux,d.channel,d.address,d.driver,
                  acquisition::name(q),q==acquisition::Quality::Valid?"true":"false",s.has_sample?"true":"false",
                  (unsigned long)s.sequence,(unsigned long)s.sampled);
        if(s.has_sample)out.print("\"age_ms\":%lu,",(unsigned long)(now-s.sampled));else out.print("\"age_ms\":null,");
        out.print("\"max_age_ms\":%lu,\"errors\":%lu,\"losses_min\":%lu,\"calibration\":\"provisional\",\"values\":",
                  (unsigned long)s.max_age(),(unsigned long)s.errors,(unsigned long)s.losses);
        if(!s.has_sample)out.print("null");
        else if(s.kind==acquisition::Kind::Env){
            out.print("{\"temperature_c\":%.3f,\"humidity_pct\":%.3f,\"pressure_pa\":%.3f,\"gas_valid\":%s,\"gas_ohm\":",
                      double(v.temp_c),double(v.rh_pct),double(v.pressure_pa),v.gas_valid&&q==acquisition::Quality::Valid?"true":"false");
            if(v.gas_valid&&q==acquisition::Quality::Valid)out.print("%.3f}",double(v.gas_ohm));else out.print("null}");
        } else if(s.kind==acquisition::Kind::Sgp)out.print("{\"eco2_estimate_ppm\":%u,\"tvoc_ppb\":%u,\"humidity_compensated\":false,\"baseline_restored\":false}",v.eco2_ppm,v.tvoc_ppb);
        else if(s.kind==acquisition::Kind::Heart)out.print("{\"ir_raw\":%u,\"red_raw\":%u,\"fifo_samples\":%u,\"nominal_hz\":50,\"fifo_age_bound_ms\":320,\"physiology_algorithm\":false}",v.ir,v.red,v.fifo_samples);
        else if(s.kind==acquisition::Kind::Tof)out.print("{\"distance_mm\":%ld,\"range_status\":%u}",(long)v.distance_mm,v.range_status);
        else if(s.kind==acquisition::Kind::Ncir)out.print("{\"ambient_centi_c\":%ld,\"object_centi_c\":%ld,\"simultaneous_sample\":false}",(long)v.ambient_centi_c,(long)v.object_centi_c);
        else out.print("{\"ch1_mv\":%d,\"ch1_ua\":%ld,\"ch2_mv\":%d,\"ch2_ua\":%ld,\"system_status\":%u,\"ch1_valid\":%s,\"ch2_valid\":%s}",v.mv1,(long)v.ua1,v.mv2,(long)v.ua2,v.io_status,(q==acquisition::Quality::Valid&&!(v.io_status&1))?"true":"false",(q==acquisition::Quality::Valid&&!(v.io_status&2))?"true":"false");
        if(s.kind==acquisition::Kind::Env)out.print(",\"last_field_status\":%u,\"last_field_index\":%u",v.env_status,v.env_index);
        out.print("}");
    }
    out.print("]}\n");out.finish();
}
void snapshot() {
    Output output;
    output.print("{\"devices\":\"inventory\",\"schema\":1,\"active_profile\":%u,\"saved_profile\":%u,"
                  "\"generation\":%lu,\"scan_age_ms\":%lu,\"scan_duration_ms\":%lu,\"complete\":%s,"
                  "\"route_ok\":%s,\"overflow\":%s,\"timed_out\":%s,\"measurement_ready\":false,\"entry_count\":%u,\"entries\":[",
                  active, saved, (unsigned long)inventory.generation, (unsigned long)(millis()-scanned_ms),
                  (unsigned long)duration_ms, inventory.complete?"true":"false", inventory.route_ok?"true":"false",
                  inventory.overflow?"true":"false", timed_out?"true":"false", unsigned(inventory.count));
    for (size_t i=0;i<inventory.count;++i) {
        const auto& d=inventory.entries[i];
        output.print("%s{\"bus\":\"%s\",\"mux\":%u,\"channel\":%d,\"address\":%u,\"driver\":\"%s\","
                      "\"state\":\"%s\",\"part\":%u,\"revision\":%u,\"unique_serial\":%s,\"serial\":\"%0*llx\"}",
                      i?",":"",d.bus?"internal":"porta",d.mux,d.channel,d.address,d.driver,
                      devices::state_name(d.state),d.part,d.revision,d.unique_serial?"true":"false",
                      !strcmp(d.driver,"mlx90614-candidate")?16:12,(unsigned long long)d.serial);
    }
    output.print("]}\n");
    output.finish();
}
}
void device_inventory_scan() {
    auto& acquisition=device_acquisition_state();bool enabled=acquisition.enabled;
    device_acquisition_pause();
    timed_out=false; external.begin(); internal.begin(); uint32_t start=millis();
    inventory.scan(external, internal, active, active_tof4m);
    duration_ms=millis()-start;scanned_ms=millis();
    if(timed_out) {
        inventory.complete=false;
        for(size_t i=0;i<inventory.count;++i)
            if(inventory.entries[i].state==devices::State::Missing) inventory.entries[i].state=devices::State::Unobserved;
    }
    device_acquisition_bind(inventory);acquisition.enabled=enabled;
}
bool device_inventory_begin() {
    Wire1.begin(2,1,100000); Wire1.setTimeOut(25);
    Preferences p;
    if(p.begin("devices",true)){ active=p.getUChar("profile",0);active_tof4m=p.getUShort("tof4m",0);p.end(); }
    if(!devices::valid_profile(active)) active=1;
    if(!devices::valid_tof4m(active_tof4m))active_tof4m=0;
    saved_tof4m=active_tof4m;saved=active;device_inventory_scan();
    device_acquisition_state().enabled=active!=1;
    for(size_t i=0;i<inventory.count;++i) {
        const auto& d=inventory.entries[i];
        if(active==0 && inventory.complete && d.bus==0 && d.channel==-1 && d.address==0x77 &&
           d.state==devices::State::Identified && d.part==0x61) return true;
    }
    return false;
}
void device_inventory_console(const char* op,const char* arg,bool extra) {
    if(extra){error("extra arguments");return;}
    if(!op || !strcmp(op,"status")){if(arg){error("unexpected argument");return;}snapshot();return;}
    if(!strcmp(op,"samples")){if(arg){error("unexpected argument");return;}samples();return;}
    if(!strcmp(op,"sampling")){
        if(!arg || (strcmp(arg,"off")&&strcmp(arg,"on"))){error("sampling: off or on");return;}
        if(!strcmp(arg,"off")){if(!device_acquisition_pause()){error("paused; sensor shutdown incomplete");return;}}
        else {if(active==1){error("observe profile does not initialize sensors");return;}
            device_acquisition_bind(inventory);device_acquisition_state().enabled=true;}
        samples();return;
    }
    if(!strcmp(op,"scan")){if(arg){error("unexpected argument");return;}device_inventory_scan();snapshot();return;}
    if(!strcmp(op,"tof4m")) {
        if(!arg){Serial.printf("{\"devices\":\"tof4m\",\"active_route\":%u,\"saved_route\":%u}\n",active_tof4m,saved_tof4m);return;}
        uint16_t wanted=0;
        if(!devices::parse_tof4m(arg,wanted)){error("tof4m: none or pahub70:0..pahub77:5; declare actual VL53L1X wiring");return;}
        Preferences p;if(!p.begin("devices",false)){error("ToF declaration storage unavailable");return;}
        bool ok=p.putUShort("tof4m",wanted)==2 && p.getUShort("tof4m",0xffff)==wanted;p.end();
        if(!ok){error("ToF declaration persistence failed");return;}
        saved_tof4m=wanted;
        Serial.printf("{\"devices\":\"tof4m_saved\",\"saved_route\":%u,\"reboot_required\":true}\n",saved_tof4m);return;
    }
    if(!strcmp(op,"profile")){
        uint8_t wanted=0;
        if(!devices::parse_profile(arg,wanted)){error("profile: observe, direct-env or pahub70..pahub77");return;}
        Preferences p;
        if(!p.begin("devices",false)){error("profile storage unavailable");return;}
        bool ok=p.putUChar("profile",wanted)==1 && p.getUChar("profile",255)==wanted;p.end();
        if(!ok){error("profile persistence failed");return;}
        saved=wanted;
        Serial.printf("{\"devices\":\"profile_saved\",\"saved_profile\":%u,\"reboot_required\":true}\n",saved);return;
    }
    if(!strcmp(op,"catalog") && !arg){
        Serial.println("{\"devices\":\"catalog\",\"schema\":1,\"discovery\":[\"pahub-configured\",\"max30100\",\"max30102\",\"sgp30\",\"bme680\",\"bme688\",\"stamplc-io-candidate\"],\"scope\":\"identity plus supported per-route input acquisition; one PaHub; IO modules 0x20..0x2f\",\"universal_compatibility\":false}");return;
    }
    error("operation: status, samples, sampling, scan, catalog, profile or tof4m");
}
