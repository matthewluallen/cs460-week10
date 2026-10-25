// Cached explicit-role telemetry plus the selected Port C HX711 driver.
// Actual VM/register mappings remain in plc_core.cpp and plc_tags.cpp.
#pragma once
#include <Arduino.h>

namespace grove {

struct Readings {
  bool  env_ok = false;
  bool  weight_ok = false;
  float tempC = 0, rh = 0, hPa = 0, gas_kohm = 0;
  int32_t weight_raw = 0;
};

class GroveUnits {
 public:
  bool begin();
  void set_env(const Readings& r) {
    r_.env_ok=r.env_ok;r_.tempC=r.tempC;r_.rh=r.rh;r_.hPa=r.hPa;r_.gas_kohm=r.gas_kohm;
  }
  void update();
  const Readings& last() const { return r_; }

  int32_t iw_weight() const   { return r_.weight_raw / 100; }
  int32_t iw_tempc_x10() const{ return (int32_t)(r_.tempC * 10.0f); }
  int32_t iw_rh_x10() const   { return (int32_t)(r_.rh * 10.0f); }
  int32_t iw_hpa() const      { return (int32_t)(r_.hPa); }

 private:
  Readings r_;
  int  hx_dout_ = 4, hx_sck_ = 5;   // Accepted physical wiring; no pin-order probing.
  bool hx_present_ = false;

};

extern GroveUnits grove;

} // namespace grove
