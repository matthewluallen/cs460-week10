// StamPLC Bus Viewer — Cardputer complementary I2C analyzer (CS460 Module 5)
// ---------------------------------------------------------------------------
// Passively SNIFFS I2C on the Cardputer's Grove port (SDA=GPIO2, SCL=GPIO1) and
// renders the decoded transactions on the 240x135 LCD — a handheld, self-contained
// twin of the virtual i2c-analyzer and the bench FX2 tap (same sigrok-style decode).
//
// Where you clip the two Grove signal lines decides what you see:
//   * StamPLC PORT.A (plug straight in) -> the sensor I2C bus (BME688 0x77)  [MODE: SENSOR]
//   * internal control bus via a Grove->breadboard adapter to the bottom
//     expansion header: SDA=GPIO13, SCL=GPIO15, + common GND -> relays/DI    [MODE: CONTROL]
//     (0x59 AW9523 relays+opto-DI, 0x43 PI4IOE buttons, 0x40/0x48/0x32 sensors)
//
// It is a PASSIVE tap: the two pins are INPUT only, never driven, so it cannot
// disturb the bus. Press E to dump captured transactions as JSON over USB serial
// (115200) to feed i2c-analyzer.html / parity_test.py.
//
// Bench-validate on real hardware. Design target is the StamPLC 400 kHz bursty bus;
// a continuously saturated 400 kHz bus may drop events (noted on screen as "drop").
// ---------------------------------------------------------------------------
#include "M5Cardputer.h"
#include <driver/gpio.h>
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"   // RTC_CNTL_OPTION1_REG / FORCE_DOWNLOAD_BOOT (buttonless reflash)

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

static void txn_commit() {
  if (!a_active) return;
  g_txn[g_txn_head] = a_cur;
  g_txn_head = (g_txn_head + 1) % TXN_MAX;
  g_txn_count++;
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
static Mode g_mode = MODE_SENSOR;   // PORT.A works on first boot; press M for the control bus
static bool g_paused = false;
static bool g_stream = false;       // serial: stream each new decoded txn as a line
static char s_line[96];             // serial command line buffer
static int  s_len = 0;

static const Dev* dev_table(int& count) {
  if (g_mode == MODE_SENSOR) { count = sizeof(SENSOR_DEVS)/sizeof(Dev); return SENSOR_DEVS; }
  count = sizeof(CONTROL_DEVS)/sizeof(Dev); return CONTROL_DEVS;
}
static const char* dev_name(uint8_t addr) {
  int n; const Dev* d = dev_table(n);
  for (int i = 0; i < n; i++) if (d[i].addr == addr) return d[i].name;
  return "?";
}

// ---- display ----
static M5Canvas g_cv(&M5Cardputer.Display);
static uint32_t g_last_draw = 0;

static uint16_t txn_color(const Txn& t) {
  if (t.read)  return TFT_GREEN;      // reads (e.g. poll DI / sensor)
  return TFT_ORANGE;                  // writes (e.g. relay latch)
}

static void draw() {
  const int W = M5Cardputer.Display.width();   // 240
  const int H = M5Cardputer.Display.height();  // 135
  g_cv.fillSprite(TFT_BLACK);

  // header
  g_cv.setTextSize(1);
  g_cv.setTextColor(TFT_WHITE, TFT_BLACK);
  g_cv.setCursor(2, 2);
  g_cv.printf("StamPLC Bus  [%s]%s", g_mode == MODE_CONTROL ? "CONTROL" : "SENSOR",
              g_paused ? " PAUSED" : "");
  g_cv.setTextColor(TFT_DARKGREY, TFT_BLACK);
  g_cv.setCursor(W - 70, 2);
  g_cv.printf("#%lu", (unsigned long)g_txn_count);
  g_cv.drawFastHLine(0, 12, W, TFT_DARKGREY);

  // lanes: one row per known device, recent activity as a moving tick
  int count; const Dev* devs = dev_table(count);
  int y = 16;
  const int laneH = 11;
  for (int i = 0; i < count && y < 16 + laneH * 6; i++, y += laneH) {
    g_cv.setTextColor(TFT_CYAN, TFT_BLACK);
    g_cv.setCursor(2, y);
    g_cv.printf("%02X %-13s", devs[i].addr, devs[i].name);
    // scan recent txns for this address, draw ticks right-to-left (newest first)
    int x = W - 4;
    for (int k = 0; k < TXN_MAX && x > 110; k++) {
      int idx = (g_txn_head - 1 - k + TXN_MAX * 2) % TXN_MAX;
      if ((uint32_t)k >= g_txn_count) break;
      const Txn& t = g_txn[idx];
      if (t.addr != devs[i].addr) continue;
      g_cv.fillRect(x - 3, y, 3, 7, txn_color(t));
      x -= 5;
    }
  }

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
    g_cv.printf("%c %02X %-11s %s", t.read ? 'R' : 'W', t.addr, dev_name(t.addr), hex);
  }

  // footer hints + drops
  g_cv.setTextColor(TFT_DARKGREY, TFT_BLACK);
  g_cv.setCursor(2, H - 9);
  g_cv.printf("M:mode  P:pause  C:clr  E:export  drop:%lu", (unsigned long)g_drops);

  g_cv.pushSprite(0, 0);
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
    Serial.printf("]}%s\n", k ? "," : "");
  }
  Serial.println("]}");
}

// ---- USB-serial control console (headless control, PLC-style) ----
// Lets this analyzer be driven entirely over USB-CDC (115200), the same way the
// StamPLC's plc> console is driven — no keyboard/LCD needed.
static void bv_help() {
  Serial.println("[HELP] help | ver | stats | export | mode sensor|control | pause | resume | clear | stream on|off | reboot | download");
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
  if      (!strcmp(ln, "help") || !strcmp(ln, "?"))   bv_help();
  else if (!strcmp(ln, "ver"))                        Serial.println("[VER] StamPLC Bus Viewer v2 (serial-controlled)");
  else if (!strcmp(ln, "stats") || !strcmp(ln, "s"))  bv_stats();
  else if (!strcmp(ln, "export") || !strcmp(ln, "e")) export_json();
  else if (!strcmp(ln, "pause"))                      { g_paused = true;  Serial.println("[OK] paused"); }
  else if (!strcmp(ln, "resume"))                     { g_paused = false; Serial.println("[OK] resumed"); }
  else if (!strcmp(ln, "clear") || !strcmp(ln, "c"))  { g_txn_head = 0; g_txn_count = 0; g_drops = 0; a_active = false; Serial.println("[OK] cleared"); }
  else if (!strcmp(ln, "mode sensor"))                { g_mode = MODE_SENSOR;  Serial.println("[OK] mode SENSOR"); }
  else if (!strcmp(ln, "mode control"))               { g_mode = MODE_CONTROL; Serial.println("[OK] mode CONTROL"); }
  else if (!strcmp(ln, "stream on"))                  { g_stream = true;  Serial.println("[OK] stream on"); }
  else if (!strcmp(ln, "stream off"))                 { g_stream = false; Serial.println("[OK] stream off"); }
  else if (!strcmp(ln, "reboot"))                     { Serial.println("[OK] rebooting"); Serial.flush(); delay(120); esp_restart(); }
  else if (!strcmp(ln, "download"))                   { // reboot into ROM download mode so esptool can reflash over USB with no BOOT button
    Serial.println("[OK] entering download mode - reflash now (esptool write-flash)"); Serial.flush(); delay(120);
#ifdef RTC_CNTL_OPTION1_REG
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
#endif
    esp_restart();
  }
  else Serial.printf("[ERR] unknown '%s' (type help)\n", ln);
}
static void handle_serial() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { s_line[s_len] = 0; if (s_len > 0) bv_exec(s_line); s_len = 0; continue; }
    if (s_len < (int)sizeof(s_line) - 1) s_line[s_len++] = (char)c;
    else { s_len = 0; Serial.println("[ERR] line too long"); }
  }
}

static void handle_keys() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  auto st = M5Cardputer.Keyboard.keysState();
  for (char c : st.word) {
    switch (c) {
      case 'm': case 'M': g_mode = (g_mode == MODE_CONTROL) ? MODE_SENSOR : MODE_CONTROL; break;
      case 'p': case 'P': g_paused = !g_paused; break;
      case 'c': case 'C': g_txn_head = 0; g_txn_count = 0; g_drops = 0; a_active = false; break;
      case 'e': case 'E': export_json(); break;
      default: break;
    }
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  g_cv.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  Serial.begin(115200);
  Serial.println();
  Serial.println("[BUSVIEW] StamPLC Bus Viewer v2 ready - USB serial control; type 'help'");

  // passive high-impedance tap — INPUT only, never drive the bus
  gpio_set_direction(PIN_SDA, GPIO_MODE_INPUT);
  gpio_set_direction(PIN_SCL, GPIO_MODE_INPUT);
  pinMode((int)PIN_SDA, INPUT);
  pinMode((int)PIN_SCL, INPUT);
  attachInterrupt(digitalPinToInterrupt((int)PIN_SCL), sclISR, RISING);
  attachInterrupt(digitalPinToInterrupt((int)PIN_SDA), sdaISR, CHANGE);
}

void loop() {
  // drain the ISR ring and decode
  if (!g_paused) {
    while (g_tail != g_head) {
      Ev e = g_ring[g_tail];
      g_tail = (g_tail + 1) & (RING - 1);
      switch (e.type) {
        case EV_START: txn_start(e.t_us); break;
        case EV_BIT:   feed_bit(e.bit);   break;
        case EV_STOP:  txn_commit();      break;
      }
    }
  }
  // serial streaming of new txns (headless live view)
  static uint32_t last_streamed = 0;
  if (g_stream && g_txn_count != last_streamed) {
    uint32_t miss = g_txn_count - last_streamed;
    uint32_t show = miss > (uint32_t)TXN_MAX ? (uint32_t)TXN_MAX : miss;
    for (uint32_t i = 0; i < show; i++) {
      int back = (int)(show - 1 - i);
      int idx = (g_txn_head - 1 - back + TXN_MAX * 2) % TXN_MAX;
      const Txn& t = g_txn[idx];
      Serial.printf("[TXN] t=%lu %c 0x%02X %s n=%u\n", (unsigned long)t.t_us, t.read ? 'R' : 'W', t.addr, dev_name(t.addr), t.n);
    }
  }
  last_streamed = g_txn_count;
  M5Cardputer.update();
  handle_keys();
  handle_serial();
  uint32_t now = millis();
  if (now - g_last_draw >= 100) { g_last_draw = now; draw(); }  // ~10 fps
}
