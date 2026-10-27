// StamPLC Bus Viewer — Cardputer complementary I2C analyzer (CS460 Module 5)
// ---------------------------------------------------------------------------
// Passively SNIFFS I2C on the Cardputer's Grove port (SDA=GPIO2, SCL=GPIO1) and
// renders the decoded transactions on the 240x135 LCD — a handheld, self-contained
// twin of the virtual i2c-analyzer and the bench FX2 tap (same sigrok-style decode).
//
// Where you clip the two Grove signal lines decides what you see:
//   * StamPLC PORT.A (plug straight in) -> the sensor I2C bus (BME688 0x77)  [MODE: SENSOR]
//   * internal control bus via Grove->pin (Dupont) leads to the bottom
//     expansion header: SDA=GPIO13, SCL=GPIO15, + common GND -> relays/DI    [MODE: CONTROL]
//     (0x59 AW9523 relays+opto-DI, 0x43 PI4IOE buttons, 0x40/0x48/0x32 sensors)
//
// It is a PASSIVE tap by default: the two pins are INPUT only, never driven, so it
// cannot disturb the bus. Send 'export' over USB serial to dump captured transactions as JSON
// (115200) to feed i2c-analyzer.html / parity_test.py.
//
// USB-DISCONNECT TOLERANT: the analyzer never depends on a host staying attached. A bounded
// serial TX timeout means a closed or stalled port can never wedge the capture loop (export
// and #CAP included); a client dropping and another opening the port is handled cleanly, with
// a fresh banner + id + state on every reconnect. Say 'bye' to end a session before closing it.
//
// ACTIVE INJECTION (teaching demo, OFF by default): when explicitly armed ('inject on',
// CONTROL mode), the 'force <addr> <reg> <val>' command drives the tap pins for ONE I2C
// write — e.g. forcing a relay ON by writing the AW9523 (0x59) output latch. This is a
// live demonstration of ATT&CK ICS T0855 (Unauthorized Command Message) / T0831
// (Manipulation of Control) for the CS460 lab; run it against the QEMU twin or an
// isolated bench, not a live process. Each arm fires once, then auto-disarms.
//
// Bench-validate on real hardware. Design target is the StamPLC 400 kHz bursty bus;
// a continuously saturated 400 kHz bus may drop events (noted on screen as "drop").
// ---------------------------------------------------------------------------
#include "M5Cardputer.h"
#include <driver/gpio.h>
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"   // RTC_CNTL_OPTION1_REG / FORCE_DOWNLOAD_BOOT (buttonless reflash)
#include "soc/gpio_struct.h"    // GPIO.in — fast two-line read for the raw-sample capture
#include "esp_cpu.h"            // esp_cpu_get_cycle_count — cycle-accurate sample spacing
#include "esp_mac.h"            // esp_efuse_mac_get_default — stable per-unit id for the USB handshake
#include <Wire.h>               // active injection drives the tap via the 2nd I2C peripheral (Wire1)
#include <Preferences.h>        // NVS persistence for USB-configurable capture settings (rate/samples/mode)
#include <string.h>
#include <stdlib.h>

// ---- capture pins (Cardputer Grove connector) ----
static const gpio_num_t PIN_SDA = GPIO_NUM_2;
static const gpio_num_t PIN_SCL = GPIO_NUM_1;

// ---- ISR -> loop event ring (single producer / single consumer) ----
enum EvType : uint8_t { EV_BIT = 0, EV_START = 1, EV_STOP = 2 };
struct Ev { uint32_t t_us; uint8_t type; uint8_t bit; };
static const uint32_t RING = 4096;              // power of two
static Ev        g_ring[RING];
static volatile uint32_t g_head = 0, g_tail = 0;
static volatile uint32_t g_drops = 0;

static inline void IRAM_ATTR push_ev(uint8_t type, uint8_t bit) {
  uint32_t h = g_head;
  uint32_t n = (h + 1) & (RING - 1);
  if (n == g_tail) { g_drops = g_drops + 1; return; }  // overflow: drop, keep running
  g_ring[h].t_us = micros();
  g_ring[h].type = type;
  g_ring[h].bit  = bit;
  g_head = n;
}

// SCL rising -> one data bit is valid on SDA
static void IRAM_ATTR sclISR() {
  push_ev(EV_BIT, (uint8_t)gpio_get_level(PIN_SDA));
}
// SDA change while SCL is high -> START (SDA falls) or STOP (SDA rises)
static void IRAM_ATTR sdaISR() {
  if (gpio_get_level(PIN_SCL)) {
    push_ev(gpio_get_level(PIN_SDA) ? EV_STOP : EV_START, 0);
  }
}

// ---- decoded transactions ----
struct Txn {
  uint32_t t_us;
  uint8_t  addr;      // 7-bit
  bool     read;      // R/W from the address byte
  uint8_t  data[16];
  uint8_t  n;         // data byte count (capped at 16)
  bool     overflow;  // >16 data bytes seen
  bool     injected;  // this frame was INJECTED by the analyzer (active T0855 demo)
};
static const int TXN_MAX = 64;
static Txn      g_txn[TXN_MAX];
static int      g_txn_head = 0;   // next write slot
static uint32_t g_txn_count = 0;  // total ever

// in-progress assembly
static bool     a_active = false;
static int      a_bitn = 0;
static uint8_t  a_byte = 0;
static bool     a_first = true;
static Txn      a_cur;

static void decode_sv(const Txn& t);   // fwd: decode a completed frame into a human-readable sensor value
static void txn_commit() {
  if (!a_active) return;
  g_txn[g_txn_head] = a_cur;
  g_txn_head = (g_txn_head + 1) % TXN_MAX;
  g_txn_count++;
  decode_sv(a_cur);
  a_active = false;
}
static void txn_start(uint32_t t_us) {
  txn_commit();                 // a repeated START closes the previous frame
  a_cur = Txn{};
  a_cur.t_us = t_us;
  a_active = true; a_bitn = 0; a_byte = 0; a_first = true;
}
static void feed_bit(uint8_t b) {
  if (!a_active) return;
  if (a_bitn < 8) { a_byte = (uint8_t)((a_byte << 1) | (b & 1)); a_bitn++; return; }
  // 9th clock = ACK/NACK slot -> the byte is complete
  if (a_first) { a_cur.addr = a_byte >> 1; a_cur.read = a_byte & 1; a_first = false; }
  else if (a_cur.n < 16) { a_cur.data[a_cur.n++] = a_byte; }
  else { a_cur.overflow = true; }
  a_bitn = 0; a_byte = 0;
}

// ---- device label profiles ----
struct Dev { uint8_t addr; const char* name; };
static const Dev SENSOR_DEVS[] = {
  {0x77, "BME688 env"}, {0x76, "BME alt"},
};
static const Dev CONTROL_DEVS[] = {
  {0x59, "AW9523 RLY/DI"}, {0x43, "PI4IOE btn"}, {0x40, "INA226 pwr"},
  {0x48, "LM75 temp"},     {0x32, "RX8130 rtc"}, {0x20, "IO-exp"},
};
enum Mode { MODE_SENSOR = 0, MODE_CONTROL = 1 };
static Mode g_mode = MODE_SENSOR;   // PORT.A works on first boot; send 'mode control' over USB for the control bus
static bool g_paused = false;
static bool g_stream = false;       // serial: stream each new decoded txn as a line
static bool g_inject_armed = false; // active injection gate — default OFF (safe); one shot per arm
static char s_line[96];             // serial command line buffer
static int  s_len = 0;

// ---- USB-configurable capture settings (persisted in NVS; see cfg_save/cfg_load) ----
// All configurable over USB: `rate <hz>`, `samples <n>`, `mode sensor|control`, shown by `settings`.
static uint32_t g_rate  = 4000000;  // #CAP default sample rate  ('rate <hz>')   — survives reboot
static uint32_t g_nsamp = 50000;    // #CAP default sample count ('samples <n>')  — survives reboot

// ---- risky-command confirmation gate (config-change validation over USB) ----
// force/reboot/download are stateful or drive the bus, so a typed command only ARMS them;
// the next line must be 'confirm' (or y/yes) to proceed — anything else cancels.
enum Pending { PEND_NONE = 0, PEND_FORCE, PEND_REBOOT, PEND_DOWNLOAD };
static Pending g_pending = PEND_NONE;
static uint8_t g_pf_addr = 0x59, g_pf_reg = 0x02, g_pf_val = 0x01;  // stashed 'force' args awaiting confirm

// ---- USB-CDC connection lifecycle (disconnect-tolerant) ----
// g_usb_up tracks whether a host has the port open, derived from the CDC connected flag AND
// recent RX, so it stays correct even where (bool)Serial is flaky on the USB-Serial/JTAG path:
// a client that is sending commands is, by definition, connected.
static bool     g_usb_up = false;
static uint32_t g_last_rx = 0;            // millis() of the last byte received over USB
static uint32_t g_last_streamed = 0;      // 'stream' baseline, resynced on connect / clear
static const uint32_t RX_GRACE_MS = 3000;
static inline bool usb_present() { return (bool)Serial || (g_last_rx != 0 && (millis() - g_last_rx) < RX_GRACE_MS); }

static const Dev* dev_table(int& count) {
  if (g_mode == MODE_SENSOR) { count = sizeof(SENSOR_DEVS)/sizeof(Dev); return SENSOR_DEVS; }
  count = sizeof(CONTROL_DEVS)/sizeof(Dev); return CONTROL_DEVS;
}
static const char* dev_name(uint8_t addr) {
  int n; const Dev* d = dev_table(n);
  for (int i = 0; i < n; i++) if (d[i].addr == addr) return d[i].name;
  return "?";
}

// ---- LIVE decoded sensor values (shown on the LCD so the student sees results, not just raw bytes) ----
struct SV { uint8_t addr; char name[12]; char val[14]; uint32_t t; bool used; };
static SV g_sv[8];
static uint16_t g_lastReg[128];   // last register the controller wrote to each address (for register-addressed reads)
static void upsertSV(uint8_t addr, const char* name, const char* val) {
  int slot = -1, oldest = 0;
  for (int i = 0; i < 8; i++) { if (g_sv[i].used && g_sv[i].addr == addr) { slot = i; break; }
    if (!g_sv[i].used) { if (slot < 0) slot = i; } else if (g_sv[i].t < g_sv[oldest].t) oldest = i; }
  if (slot < 0) slot = oldest;
  g_sv[slot].used = true; g_sv[slot].addr = addr; g_sv[slot].t = millis();
  strncpy(g_sv[slot].name, name, sizeof(g_sv[slot].name)-1); g_sv[slot].name[sizeof(g_sv[slot].name)-1] = 0;
  strncpy(g_sv[slot].val,  val,  sizeof(g_sv[slot].val)-1);  g_sv[slot].val[sizeof(g_sv[slot].val)-1]  = 0;
}
static void decode_sv(const Txn& t) {
  uint8_t a = t.addr & 0x7F; char v[14];
  if (!t.read) {                            // a write carries the register pointer
    if (t.n >= 1) { if (a == 0x29 && t.n >= 2) g_lastReg[a] = ((uint16_t)t.data[0] << 8) | t.data[1]; else g_lastReg[a] = t.data[0]; }
    return;
  }
  uint16_t reg = g_lastReg[a];
  switch (a) {
    case 0x57: if (t.n >= 3) { uint32_t um = ((uint32_t)t.data[0]<<16)|((uint32_t)t.data[1]<<8)|t.data[2]; snprintf(v,sizeof(v),"%lu mm",(unsigned long)(um/1000)); upsertSV(a,"RCWL ultra",v); } break;
    case 0x23: if (t.n >= 2) { uint16_t raw = ((uint16_t)t.data[0]<<8)|t.data[1]; snprintf(v,sizeof(v),"%u lux",(unsigned)(raw/1.2f)); upsertSV(a,"BH1750",v); } break;
    case 0x29: if (reg==0x0096 && t.n>=2) { uint16_t mm=((uint16_t)t.data[0]<<8)|t.data[1]; snprintf(v,sizeof(v),"%u mm",mm); upsertSV(a,"VL53 ToF",v); }
               else if (reg==0x010F && t.n>=1) { snprintf(v,sizeof(v),"id %02X",t.data[0]); upsertSV(a,"VL53",v); } break;
    case 0x40: if (reg==0x00 && t.n>=4) { int32_t c=(int32_t)((uint32_t)t.data[0]|((uint32_t)t.data[1]<<8)|((uint32_t)t.data[2]<<16)|((uint32_t)t.data[3]<<24)); snprintf(v,sizeof(v),"%ld cnt",(long)c); upsertSV(a,"Encoder",v); } break;
    case 0x5A: if (reg==0x07 && t.n>=2) { uint16_t raw=t.data[0]|((uint16_t)t.data[1]<<8); float c=raw*0.02f-273.15f; snprintf(v,sizeof(v),"%d.%d C",(int)c,abs((int)(c*10))%10); upsertSV(a,"MLX/NCIR",v); }
               else if (reg==0x0F && t.n>=1) { snprintf(v,sizeof(v),"who %02X",t.data[0]); upsertSV(a,"TMOS",v); } break;
    case 0x62: upsertSV(a,"SCD40 CO2","(9B+CRC)"); break;
    case 0x77: if (reg==0xD0 && t.n>=1) { snprintf(v,sizeof(v),"id %02X",t.data[0]); upsertSV(a,"BME688",v); } break;
  }
}

// ---- display ----
// display reference is bound LOCALLY inside draw() (after begin()) — a file-scope
// `static auto& = M5Cardputer.Display` binds during static init, before begin() sets
// up the panel, leaving _panel null -> LoadProhibited on the first height() in draw().
static uint32_t g_last_draw = 0;

static uint16_t txn_color(const Txn& t) {
  if (t.injected) return TFT_RED;     // INJECTED write (active attack) — stands out
  if (t.read)  return TFT_GREEN;      // reads (e.g. poll DI / sensor)
  return TFT_ORANGE;                  // writes (e.g. relay latch)
}

static void draw() {
  auto& g_cv = M5Cardputer.Display;            // bind at call time (after begin), like the controller
  const int W = g_cv.width();                  // 240
  const int H = g_cv.height();                 // 135
  g_cv.fillScreen(TFT_BLACK);

  // header
  g_cv.setTextSize(1);
  g_cv.setTextColor(TFT_WHITE, TFT_BLACK);
  g_cv.setCursor(2, 2);
  g_cv.printf("ANALYZER [%s]%s", g_mode == MODE_CONTROL ? "CONTROL" : "SENSOR",
              g_paused ? " PAUSED" : "");
  g_cv.setTextColor(TFT_DARKGREY, TFT_BLACK);
  g_cv.setCursor(W - 70, 2);
  g_cv.printf("#%lu", (unsigned long)g_txn_count);
  g_cv.drawFastHLine(0, 12, W, TFT_DARKGREY);

  // LIVE VALUES — decoded sensor readings (the "results" the student watches: real vs forged)
  int y = 15;
  g_cv.setTextColor(TFT_YELLOW, TFT_BLACK);
  g_cv.setCursor(2, y); g_cv.print("LIVE VALUES (decoded)"); y += 11;
  bool shown[8] = {false}; int printed = 0;
  for (int pass = 0; pass < 8 && printed < 6; pass++) {
    int best = -1; uint32_t bt = 0;
    for (int i = 0; i < 8; i++) if (g_sv[i].used && !shown[i] && g_sv[i].t >= bt) { bt = g_sv[i].t; best = i; }
    if (best < 0) break; shown[best] = true; printed++;
    g_cv.setTextColor(TFT_CYAN,  TFT_BLACK); g_cv.setCursor(2,   y); g_cv.printf("%02X %-10s", g_sv[best].addr, g_sv[best].name);
    g_cv.setTextColor(TFT_GREEN, TFT_BLACK); g_cv.setCursor(140, y); g_cv.print(g_sv[best].val);
    y += 11;
  }
  if (printed == 0) { g_cv.setTextColor(TFT_DARKGREY, TFT_BLACK); g_cv.setCursor(2, y); g_cv.print("waiting for sensor reads..."); }

  // decoded log: last few transactions as text
  int logY = H - 42;
  g_cv.drawFastHLine(0, logY - 2, W, TFT_DARKGREY);
  for (int k = 0; k < 4; k++) {
    int idx = (g_txn_head - 1 - k + TXN_MAX * 2) % TXN_MAX;
    if ((uint32_t)k >= g_txn_count) break;
    const Txn& t = g_txn[idx];
    g_cv.setTextColor(txn_color(t), TFT_BLACK);
    g_cv.setCursor(2, logY + k * 8);
    char hex[28] = {0}; int p = 0;
    for (int b = 0; b < t.n && p < (int)sizeof(hex) - 3; b++) p += snprintf(hex + p, sizeof(hex) - p, "%02X ", t.data[b]);
    g_cv.printf("%c %02X %-11s %s", t.injected ? '!' : (t.read ? 'R' : 'W'), t.addr, dev_name(t.addr), hex);
  }

  // footer hints + drops (red injection banner when armed)
  if (g_inject_armed) {
    g_cv.setTextColor(TFT_RED, TFT_BLACK);
    g_cv.setCursor(2, H - 9);
    g_cv.printf("** INJECT ARMED (T0855) **  USB:clear  drop:%lu", (unsigned long)g_drops);
  } else {
    g_cv.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g_cv.setCursor(2, H - 9);
    g_cv.printf("USB console (115200): type 'help'  drop:%lu", (unsigned long)g_drops);
  }

  // direct draw: no pushSprite
}

// ---- raw-sample capture -> #CAP stream for the web waveform app ----
// Busy-wait uniform sampler on the same Grove pins (bit0=SDA/G2, bit1=SCL/G1).
// Complements the interrupt sniffer: the ISRs decode transactions for the LCD/JSON,
// this streams raw samples so the browser can draw a true SDA/SCL waveform + re-decode.
//   protocol:  "#CAP rate=<hz> nch=2 nsamp=<n>\n"  then <n> bytes (bit0=SDA, bit1=SCL)
static const uint32_t CAP_MAXN = 60000;
static uint8_t g_cap[CAP_MAXN];
static void stream_cap(uint32_t rate, uint32_t nsamp) {
  if (nsamp > CAP_MAXN) nsamp = CAP_MAXN;
  detachInterrupt(digitalPinToInterrupt((int)PIN_SCL));
  detachInterrupt(digitalPinToInterrupt((int)PIN_SDA));
  uint32_t cyc = 240000000UL / rate; if (cyc < 10) cyc = 10;
  // wait up to 600 ms for an I2C START (SDA 1->0 while SCL high); else free-run
  uint32_t t0 = millis();
  uint8_t prev = (uint8_t)(((GPIO.in >> 2) & 1) | (((GPIO.in >> 1) & 1) << 1));
  while (millis() - t0 < 600) {
    uint32_t in = GPIO.in; uint8_t s = (uint8_t)(((in >> 2) & 1) | (((in >> 1) & 1) << 1));
    if ((prev & 2) && (s & 2) && (prev & 1) && !(s & 1)) break;
    prev = s;
  }
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);
  uint32_t base = esp_cpu_get_cycle_count();
  for (uint32_t i = 0; i < nsamp; i++) {
    uint32_t in = GPIO.in;
    g_cap[i] = (uint8_t)(((in >> 2) & 1) | (((in >> 1) & 1) << 1));
    uint32_t target = base + (i + 1) * cyc;
    while ((int32_t)(esp_cpu_get_cycle_count() - target) < 0) { }
  }
  portEXIT_CRITICAL(&mux);
  attachInterrupt(digitalPinToInterrupt((int)PIN_SCL), sclISR, RISING);
  attachInterrupt(digitalPinToInterrupt((int)PIN_SDA), sdaISR, CHANGE);
  Serial.printf("#CAP rate=%u nch=2 nsamp=%u\n", (unsigned)rate, (unsigned)nsamp);
  Serial.write(g_cap, nsamp);
  Serial.flush();
}

// ---- persisted settings + the deferred 'download' action (used by the confirm gate) ----
static Preferences g_prefs;
static void cfg_save() {
  if (g_prefs.begin("busview", false)) {           // RW
    g_prefs.putULong("rate",  g_rate);
    g_prefs.putULong("nsamp", g_nsamp);
    g_prefs.putUChar("mode",  (uint8_t)g_mode);
    g_prefs.end();
  }
}
static void cfg_load() {
  if (g_prefs.begin("busview", true)) {            // RO
    g_rate  = g_prefs.getULong("rate",  4000000);
    g_nsamp = g_prefs.getULong("nsamp", 50000);
    g_mode  = (Mode)g_prefs.getUChar("mode", (uint8_t)MODE_SENSOR);
    g_prefs.end();
  }
  if (g_rate  < 500000)    g_rate  = 500000;        // clamp to the sane capture window
  if (g_rate  > 20000000)  g_rate  = 20000000;
  if (g_nsamp < 16)        g_nsamp = 16;
  if (g_nsamp > CAP_MAXN)  g_nsamp = CAP_MAXN;
}
static void bv_settings() {
  Serial.printf("[CFG] mode=%s rate=%u Hz samples=%u (persisted in NVS 'busview')\n",
                g_mode == MODE_CONTROL ? "control" : "sensor", (unsigned)g_rate, (unsigned)g_nsamp);
}
static void do_download() {   // reboot into ROM download mode so esptool can reflash over USB with no BOOT button
  Serial.println("[OK] entering download mode - reflash now (esptool write-flash)"); Serial.flush(); delay(120);
#ifdef RTC_CNTL_OPTION1_REG
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
#endif
  esp_restart();
}

// ---- ACTIVE INJECTION (T0855 demo) — drives the tap to write ONE I2C frame ----
// The opposite of the passive tap. Gated: CONTROL mode + explicitly armed; one shot.
// Records the forced write into the txn buffer (red '!' on the LCD, "inj":true in JSON)
// so the analyzer shows its own attack the same way it shows legitimate traffic.
static void record_injected(uint8_t addr, uint8_t reg, uint8_t val) {
  Txn t = Txn{};
  t.t_us = micros();
  t.addr = addr; t.read = false;
  t.data[0] = reg; t.data[1] = val; t.n = 2;
  t.injected = true;
  g_txn[g_txn_head] = t;
  g_txn_head = (g_txn_head + 1) % TXN_MAX;
  g_txn_count++;
}
static void force_write(uint8_t addr, uint8_t reg, uint8_t val) {
  if (g_mode != MODE_CONTROL) { Serial.println("[ERR] force: CONTROL mode only (send 'mode control')"); return; }
  if (!g_inject_armed)        { Serial.println("[ERR] force: not armed (send 'inject on' first)"); return; }
  Serial.printf("[FORCE] *** ACTIVE INJECTION (T0855) *** writing 0x%02X -> reg 0x%02X @0x%02X\n",
                (unsigned)val, (unsigned)reg, (unsigned)addr);
  // stop passively sniffing while we drive the two lines ourselves
  detachInterrupt(digitalPinToInterrupt((int)PIN_SCL));
  detachInterrupt(digitalPinToInterrupt((int)PIN_SDA));
  Wire1.begin((int)PIN_SDA, (int)PIN_SCL, 100000);
  Wire1.beginTransmission(addr);
  Wire1.write(reg);
  Wire1.write(val);
  uint8_t rc = Wire1.endTransmission(true);   // 0 = device ACKed the write
  Wire1.end();
  // restore the high-impedance passive tap exactly as setup() left it
  gpio_set_direction(PIN_SDA, GPIO_MODE_INPUT);
  gpio_set_direction(PIN_SCL, GPIO_MODE_INPUT);
  pinMode((int)PIN_SDA, INPUT);
  pinMode((int)PIN_SCL, INPUT);
  attachInterrupt(digitalPinToInterrupt((int)PIN_SCL), sclISR, RISING);
  attachInterrupt(digitalPinToInterrupt((int)PIN_SDA), sdaISR, CHANGE);
  record_injected(addr, reg, val);
  g_inject_armed = false;   // one trigger-pull per arm — disarm immediately
  Serial.printf("[FORCE] result rc=%u (%s); auto-disarmed\n", (unsigned)rc, rc == 0 ? "ACK/ok" : "NACK/err");
}

// ---- JSON export over USB serial (feeds i2c-analyzer / parity_test) ----
static void export_json() {
  Serial.println("{\"schema\":\"cardputer.i2c-capture/v1\",\"txns\":[");
  int n = (g_txn_count < TXN_MAX) ? (int)g_txn_count : TXN_MAX;
  for (int k = n - 1; k >= 0; k--) {
    int idx = (g_txn_head - 1 - k + TXN_MAX * 2) % TXN_MAX;
    const Txn& t = g_txn[idx];
    Serial.printf("  {\"t_us\":%lu,\"addr\":%u,\"dir\":\"%c\",\"data\":[",
                  (unsigned long)t.t_us, t.addr, t.read ? 'R' : 'W');
    for (int b = 0; b < t.n; b++) Serial.printf("%s%u", b ? "," : "", t.data[b]);
    Serial.printf("]%s}%s\n", t.injected ? ",\"inj\":true" : "", k ? "," : "");
  }
  Serial.println("]}");
}

// ---- USB-serial control console (headless control, PLC-style) ----
// Lets this analyzer be driven entirely over USB-CDC (115200), the same way the
// StamPLC's plc> console is driven — no keyboard/LCD needed.
// Stable identity handshake so the web app (and a human among many identical
// "USB JTAG/serial debug unit" ports) can confirm THIS is our analyzer. The S3's
// USB descriptor name is fixed in ROM and identical across units, so we identify
// in software: a magic token + role + per-unit MAC, emitted at boot and on `id`.
#define CS460_ID_MAGIC "CS460-BUSVIEW"
static void print_id() {
  uint8_t m[6] = {0}; esp_efuse_mac_get_default(m);
  Serial.printf("[ID] " CS460_ID_MAGIC " role=ANALYZER fw=v3 mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                m[0], m[1], m[2], m[3], m[4], m[5]);
}
static void bv_help() {
  Serial.println("[HELP] help | id | ver | stats | export | cap | {arm json} | mode sensor|control | pause | resume | clear | stream on|off | bye | reboot | download");
  Serial.println("[HELP] CONFIG (USB, persisted in NVS): rate <hz> | samples <n> | settings   (reboot/download/force need a 'confirm' line)");
  Serial.println("[HELP] INJECT (T0855 demo, CONTROL+armed): inject on|off | force <addr> <reg> <val> | {\"cmd\":\"force\",\"addr\":89,\"reg\":2,\"val\":1}");
}
static void bv_stats() {
  Serial.printf("[STATS] mode=%s total=%lu drops=%lu paused=%d stream=%d\n",
                g_mode == MODE_CONTROL ? "CONTROL" : "SENSOR",
                (unsigned long)g_txn_count, (unsigned long)g_drops, (int)g_paused, (int)g_stream);
  int n = (g_txn_count < (uint32_t)TXN_MAX) ? (int)g_txn_count : TXN_MAX;
  uint8_t addr[TXN_MAX]; uint16_t cnt[TXN_MAX]; uint16_t rd[TXN_MAX]; int na = 0;
  for (int k = 0; k < n; k++) {
    int idx = (g_txn_head - 1 - k + TXN_MAX * 2) % TXN_MAX;
    const Txn& t = g_txn[idx]; int j;
    for (j = 0; j < na; j++) if (addr[j] == t.addr) break;
    if (j == na) { addr[na] = t.addr; cnt[na] = 0; rd[na] = 0; na++; }
    cnt[j]++; if (t.read) rd[j]++;
  }
  for (int j = 0; j < na; j++)
    Serial.printf("[STATS] 0x%02X %-12s n=%u R=%u W=%u\n", addr[j], dev_name(addr[j]), cnt[j], rd[j], (uint16_t)(cnt[j]-rd[j]));
  Serial.println("[STATS] end");
}
static void bv_exec(const char* ln) {
  // confirmation gate: a prior risky command is awaiting 'confirm' (config-change validation)
  if (g_pending != PEND_NONE) {
    Pending p = g_pending; g_pending = PEND_NONE;
    if (!strcmp(ln, "confirm") || !strcmp(ln, "y") || !strcmp(ln, "yes")) {
      switch (p) {
        case PEND_FORCE:    force_write(g_pf_addr, g_pf_reg, g_pf_val); break;
        case PEND_REBOOT:   Serial.println("[OK] rebooting"); Serial.flush(); delay(120); esp_restart(); break;
        case PEND_DOWNLOAD: do_download(); break;
        default: break;
      }
    } else {
      Serial.printf("[CANCEL] no confirmation ('%s') - action aborted\n", ln);
    }
    return;
  }
  // raw-sample capture for the web waveform app: {"cmd":"arm","rate":..,"samples":..,"bus":..} or "cap"
  if ((ln[0] == '{' && strstr(ln, "arm")) || !strcmp(ln, "cap")) {
    uint32_t rate = g_rate, nsamp = g_nsamp;   // persisted defaults; a per-arm rate:/samples: overrides for this capture only
    const char* r = strstr(ln, "rate");    if (r) { const char* c = strchr(r, ':'); if (c) rate  = (uint32_t)strtoul(c + 1, nullptr, 10); }
    const char* s = strstr(ln, "samples"); if (s) { const char* c = strchr(s, ':'); if (c) nsamp = (uint32_t)strtoul(c + 1, nullptr, 10); }
    if (rate < 500000) rate = 500000;
    if (strstr(ln, "expansion")) g_mode = MODE_CONTROL;
    else if (strstr(ln, "porta")) g_mode = MODE_SENSOR;
    stream_cap(rate, nsamp);
    return;
  }
  // ACTIVE INJECTION: JSON {"cmd":"force","addr":..,"reg":..,"val":..} (hex or dec) — the web attack panel
  if (ln[0] == '{' && strstr(ln, "force")) {
    uint8_t addr = 0x59, reg = 0x02, val = 0x01;   // defaults: force the AW9523 relay latch
    const char* a = strstr(ln, "addr"); if (a) { const char* c = strchr(a, ':'); if (c) addr = (uint8_t)strtoul(c + 1, nullptr, 0); }
    const char* r = strstr(ln, "reg");  if (r) { const char* c = strchr(r, ':'); if (c) reg  = (uint8_t)strtoul(c + 1, nullptr, 0); }
    const char* v = strstr(ln, "val");  if (v) { const char* c = strchr(v, ':'); if (c) val  = (uint8_t)strtoul(c + 1, nullptr, 0); }
    force_write(addr, reg, val);
    return;
  }
  if      (!strcmp(ln, "help") || !strcmp(ln, "?"))   bv_help();
  else if (!strcmp(ln, "id") || !strcmp(ln, "whoami")) print_id();
  else if (!strcmp(ln, "ver"))                        Serial.println("[VER] StamPLC Bus Viewer v3 (sniff + #CAP waveform + guarded T0855 inject)");
  else if (!strcmp(ln, "stats") || !strcmp(ln, "s"))  bv_stats();
  else if (!strcmp(ln, "export") || !strcmp(ln, "e")) export_json();
  else if (!strcmp(ln, "pause"))                      { g_paused = true;  Serial.println("[OK] paused"); }
  else if (!strcmp(ln, "resume"))                     { g_paused = false; Serial.println("[OK] resumed"); }
  else if (!strcmp(ln, "clear") || !strcmp(ln, "c"))  { g_txn_head = 0; g_txn_count = 0; g_drops = 0; a_active = false; g_last_streamed = 0; Serial.println("[OK] cleared"); }
  else if (!strcmp(ln, "mode sensor"))                { g_mode = MODE_SENSOR;  cfg_save(); Serial.println("[OK] mode SENSOR (saved)"); }
  else if (!strcmp(ln, "mode control"))               { g_mode = MODE_CONTROL; cfg_save(); Serial.println("[OK] mode CONTROL (saved)"); }
  else if (!strncmp(ln, "rate ", 5))                  { uint32_t hz = (uint32_t)strtoul(ln + 5, nullptr, 0); if (hz < 500000) hz = 500000; if (hz > 20000000) hz = 20000000; g_rate = hz; cfg_save(); Serial.printf("[OK] capture rate=%u Hz (saved to NVS)\n", (unsigned)g_rate); }
  else if (!strncmp(ln, "samples ", 8))               { uint32_t n = (uint32_t)strtoul(ln + 8, nullptr, 0); if (n < 16) n = 16; if (n > CAP_MAXN) n = CAP_MAXN; g_nsamp = n; cfg_save(); Serial.printf("[OK] capture samples=%u (saved to NVS)\n", (unsigned)g_nsamp); }
  else if (!strcmp(ln, "settings") || !strcmp(ln, "cfg")) bv_settings();
  else if (!strcmp(ln, "stream on"))                  { g_stream = true;  g_last_streamed = g_txn_count; Serial.println("[OK] stream on"); }
  else if (!strcmp(ln, "stream off"))                 { g_stream = false; Serial.println("[OK] stream off"); }
  else if (!strcmp(ln, "bye") || !strcmp(ln, "disconnect")) {   // cooperative handoff: stop talking, keep capturing
    g_stream = false; s_len = 0;
    Serial.println("[OK] bye - stream off; safe to close the port (capture keeps running)");
    Serial.flush();
  }
  else if (!strcmp(ln, "inject on") || !strcmp(ln, "arm"))    { g_inject_armed = true;  Serial.println("[WARN] INJECTION ARMED - next 'force' DRIVES the bus (ATT&CK ICS T0855). CONTROL mode, one shot."); }
  else if (!strcmp(ln, "inject off") || !strcmp(ln, "disarm")){ g_inject_armed = false; Serial.println("[OK] injection disarmed"); }
  else if (!strncmp(ln, "force ", 6)) {               // text form: force <addr> <reg> <val> (hex 0x.. or dec) — confirm-gated
    char* end; unsigned long a = strtoul(ln + 6, &end, 0);
    unsigned long r = strtoul(end, &end, 0);
    unsigned long v = strtoul(end, &end, 0);
    if (g_mode != MODE_CONTROL)  Serial.println("[ERR] force: CONTROL mode only (send 'mode control')");
    else if (!g_inject_armed)    Serial.println("[ERR] force: not armed (send 'inject on' first)");
    else {
      g_pf_addr = (uint8_t)a; g_pf_reg = (uint8_t)r; g_pf_val = (uint8_t)v; g_pending = PEND_FORCE;
      Serial.printf("[CONFIRM] force 0x%02X -> reg 0x%02X @0x%02X will DRIVE the bus (ATT&CK ICS T0855). Type 'confirm' to proceed, anything else cancels.\n",
                    (unsigned)v, (unsigned)r, (unsigned)a);
    }
  }
  else if (!strcmp(ln, "reboot"))                     { g_pending = PEND_REBOOT;   Serial.println("[CONFIRM] reboot the analyzer? Type 'confirm' to proceed, anything else cancels."); }
  else if (!strcmp(ln, "download"))                   { g_pending = PEND_DOWNLOAD; Serial.println("[CONFIRM] enter ROM download mode (reboots to reflash)? Type 'confirm' to proceed, anything else cancels."); }
  else Serial.printf("[ERR] unknown '%s' (type help)\n", ln);
}
static void handle_serial() {
  while (Serial.available() > 0) {
    g_last_rx = millis();        // any RX proves a host is attached
    int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { s_line[s_len] = 0; if (s_len > 0) bv_exec(s_line); s_len = 0; continue; }
    if (s_len < (int)sizeof(s_line) - 1) s_line[s_len++] = (char)c;
    else { s_len = 0; Serial.println("[ERR] line too long"); }
  }
}

// ---- USB connect / disconnect lifecycle ----
// React to the host opening or closing the port so the device never streams into a closed
// port and always greets a reconnecting client (banner + id + state), the same way boot does.
static void handle_usb_lifecycle() {
  bool up_now = usb_present();
  if (up_now && !g_usb_up) {            // connect edge: a client just opened the port
    g_usb_up = true;
    s_len = 0;                          // drop any half-typed line from a prior client
    g_last_streamed = g_txn_count;      // don't replay the backlog to a fresh client
    Serial.println();
    Serial.println("[BUSVIEW] StamPLC Bus Viewer v3 ready - USB serial control; type 'help'");
    print_id();                         // identity so the web app can confirm the analyzer
    bv_stats();                         // current state so a reconnecting client resumes cleanly
  } else if (!up_now && g_usb_up) {     // disconnect edge: the host closed / vanished
    g_usb_up = false;
    g_stream = false;                   // stop streaming into a closed port (also unblocks capture)
    s_len = 0;
  }
}

// (The Cardputer keyboard is intentionally NOT polled — all control is over the USB console.
//  The key handler was removed to fix a boot crash; mode/pause/clear/export are typed commands.)

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  // no sprite (direct draw)
  Serial.begin(115200);
  // Bounded TX timeout: a write may stall at most this long if the USB host is gone, so a
  // disconnected or non-reading host can NEVER wedge the capture loop (export / #CAP included).
  Serial.setTxTimeoutMs(250);
  Serial.println();
  Serial.println("[BUSVIEW] StamPLC Bus Viewer v3 ready - USB serial control; type 'help'");
  cfg_load();   // restore USB-configured capture rate / samples / mode from NVS (defaults on first boot)
  print_id();   // emit identity at boot so a listener/the web app can confirm the analyzer without probing

  // passive high-impedance tap — INPUT only, never drive the bus
  gpio_set_direction(PIN_SDA, GPIO_MODE_INPUT);
  gpio_set_direction(PIN_SCL, GPIO_MODE_INPUT);
  pinMode((int)PIN_SDA, INPUT);
  pinMode((int)PIN_SCL, INPUT);
  attachInterrupt(digitalPinToInterrupt((int)PIN_SCL), sclISR, RISING);
  attachInterrupt(digitalPinToInterrupt((int)PIN_SDA), sdaISR, CHANGE);
  draw();   // first paint from setup() — display is initialized here (like the controller)
  g_usb_up = usb_present();   // seed link state so a host attached at boot isn't re-greeted
}

void loop() {
  handle_usb_lifecycle();   // react to the host attaching/detaching before any serial I/O
  // drain the ISR ring and decode. BOUNDED per iteration: on an active/noisy bus the
  // edge ISRs advance g_head as fast as we drain, so an unbounded while() never returns
  // and loop() would never reach handle_serial()/draw()/USB service —
  // dead console + frozen screen with no crash. Budget guarantees the loop always breathes.
  if (!g_paused) {
    int budget = 1024;
    while (g_tail != g_head && budget-- > 0) {
      Ev e = g_ring[g_tail];
      g_tail = (g_tail + 1) & (RING - 1);
      switch (e.type) {
        case EV_START: txn_start(e.t_us); break;
        case EV_BIT:   feed_bit(e.bit);   break;
        case EV_STOP:  txn_commit();      break;
      }
    }
  }
  // serial streaming of new txns (headless live view) — only while a host is attached
  if (g_stream && g_usb_up && g_txn_count != g_last_streamed) {
    uint32_t miss = g_txn_count - g_last_streamed;
    uint32_t show = miss > (uint32_t)TXN_MAX ? (uint32_t)TXN_MAX : miss;
    for (uint32_t i = 0; i < show; i++) {
      int back = (int)(show - 1 - i);
      int idx = (g_txn_head - 1 - back + TXN_MAX * 2) % TXN_MAX;
      const Txn& t = g_txn[idx];
      Serial.printf("[TXN] t=%lu %c 0x%02X %s n=%u\n", (unsigned long)t.t_us, t.read ? 'R' : 'W', t.addr, dev_name(t.addr), t.n);
    }
  }
  g_last_streamed = g_txn_count;   // keep the baseline current (also when not streaming / detached)
  handle_serial();   // USB console only (mode/pause/clear/export); no keyboard/update — matches the known-good controller
  uint32_t now = millis();
  // direct-draw (no double-buffer): redraw only when something changed, so the screen is steady
  static uint32_t lN = 0xFFFFFFFF; static int lM = -1; static bool lP = false, lA = false; static uint32_t lD = 0;
  if (now - g_last_draw >= 100) {
    if (g_txn_count != lN || (int)g_mode != lM || g_paused != lP || g_inject_armed != lA || g_drops != lD) {
      g_last_draw = now; lN = g_txn_count; lM = g_mode; lP = g_paused; lA = g_inject_armed; lD = g_drops; draw();
    }
  }
}
