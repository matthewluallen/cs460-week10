// grove_units.cpp — see grove_units.h.
#include "grove_units.h"
#include "port_c.h"
#include <Wire.h>


// StamPLC Grove ports: RED sda=2/scl=1 (BME688 I2C on Wire1); BLUE pins 5/4 (HX711 GPIO)
#define GROVE_RED_SDA   2
#define GROVE_RED_SCL   1
#define BLUE_P1         5      // BLUE port SDA-position pin (yellow, usually DAT)
#define BLUE_P2         4      // BLUE port SCL-position pin (white,  usually CLK)
#define BME688_ADDR     0x77

namespace grove {

GroveUnits grove;


// Perform one HX711 read on a candidate (dout,sck). Returns true only if the line
// behaved like DOUT: it fell LOW (data ready) within the window and returned HIGH
// after the 25-clock readout — the HX711 protocol signature. Value in *out.
static bool hx711_try(int dout, int sck, int32_t* out) {
  pinMode(sck, OUTPUT); digitalWrite(sck, LOW);
  pinMode(dout, INPUT);
  uint32_t t0 = millis();
  while (digitalRead(dout) == HIGH) { if (millis() - t0 > 500) return false; }  // never ready
  uint32_t v = 0;
  for (int i = 0; i < 24; i++) {
    digitalWrite(sck, HIGH); delayMicroseconds(1);
    v = (v << 1) | (digitalRead(dout) & 1);
    digitalWrite(sck, LOW);  delayMicroseconds(1);
  }
  digitalWrite(sck, HIGH); delayMicroseconds(1);   // 25th pulse -> gain 128, ch A
  digitalWrite(sck, LOW);  delayMicroseconds(1);
  delayMicroseconds(5);
  bool high_after = (digitalRead(dout) == HIGH);    // DOUT returns HIGH after readout
  if (v & 0x800000) v |= 0xFF000000;                // sign-extend 24-bit
  *out = (int32_t)v;
  return high_after;
}

bool GroveUnits::begin() {
  // Port A conversion scheduling belongs to device_acquisition.
  // Only the explicitly selected HX711 profile may drive Port C. Never try
  // a second wiring order: that could drive another device's output conductor.
  hx_present_ = false;
  r_.weight_ok = false;
  if (port_c_profile() != PortCProfile::HX711) {
    Serial.println("[GROVE] HX711 disabled by Port C profile");
    return true;
  }
  delay(200);
  int32_t v = 0;
  if (hx711_try(BLUE_P2, BLUE_P1, &v)) {
    hx_dout_ = BLUE_P2; hx_sck_ = BLUE_P1; hx_present_ = true;
  } else {
    hx_present_ = false;
  }
  if (hx_present_) {
    r_.weight_raw = v;
    Serial.printf("[GROVE] HX711 online on BLUE port (DAT=%d CLK=%d) raw=%ld\n",
                  hx_dout_, hx_sck_, (long)v);
  } else {
    // leave pins in a safe state and report both raw levels for diagnosis
    pinMode(BLUE_P1, INPUT); pinMode(BLUE_P2, INPUT);
    Serial.printf("[GROVE] HX711 not responding on BLUE (pin%d=%d pin%d=%d) - check DAT/CLK seating & 5V\n",
                  BLUE_P1, digitalRead(BLUE_P1), BLUE_P2, digitalRead(BLUE_P2));
  }
  return true;
}

void GroveUnits::update() {
  if (hx_present_) {
    int32_t v;
    r_.weight_ok = hx711_try(hx_dout_, hx_sck_, &v);
    if (r_.weight_ok) r_.weight_raw = v;
  }
}

} // namespace grove
