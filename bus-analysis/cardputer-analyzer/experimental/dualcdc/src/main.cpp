// Dual-CDC reference — two independent USB serial consoles on ONE ESP32-S3 USB cable.
// ---------------------------------------------------------------------------------
// WHY: the production analyzer talks over the ESP32-S3 USB-Serial/JTAG peripheral
// (ARDUINO_USB_MODE=1), which is a SINGLE CDC interface — so macOS gives exactly one
// process the port, and the WebSerial app and pyserial can't both hold it. The S3's
// *other* USB path (TinyUSB, ARDUINO_USB_MODE=0) can enumerate a COMPOSITE device with
// two CDC-ACM interfaces (CFG_TUD_CDC=2), i.e. two /dev/cu.usbmodem* ports from one cable.
//
// This sketch proves that: port 0 and port 1 are serviced independently, each with its own
// line buffer and echo. Open both in two terminals (or one in the web app, one in pyserial)
// and watch them run at the same time. THIS IS A REFERENCE, not the analyzer — see README.
#include <Arduino.h>
#include "USB.h"
#include "USBCDC.h"

USBCDC Port0(0);   // intended role: the WebSerial analyzer app
USBCDC Port1(1);   // intended role: a serial client (pyserial / terminal)

static char b0[96]; static int n0 = 0;
static char b1[96]; static int n1 = 0;

// One console serviced on an arbitrary CDC port — this is the pattern the full analyzer
// would use: run the SAME command parser on each port, writing replies to the port the
// command arrived on (so neither client sees the other's I/O).
static void service(USBCDC& p, char* buf, int& n, const char* tag) {
  while (p.available() > 0) {
    int c = p.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[n] = 0;
      if (n > 0) {
        if      (!strcmp(buf, "id"))   p.printf("[%s] CS460-BUSVIEW dual-cdc reference\n", tag);
        else if (!strcmp(buf, "help")) p.printf("[%s] help | id | ping (each port is independent)\n", tag);
        else if (!strcmp(buf, "ping")) p.printf("[%s] pong\n", tag);
        else                            p.printf("[%s] echo: %s\n", tag, buf);
      }
      n = 0;
    } else if (n < (int)sizeof(b0) - 1) buf[n++] = (char)c;
    else { n = 0; p.printf("[%s] line too long\n", tag); }
  }
}

void setup() {
  USB.begin();
  Port0.setTxTimeoutMs(250);
  Port1.setTxTimeoutMs(250);
  Port0.begin();
  Port1.begin();
}

void loop() {
  service(Port0, b0, n0, "P0");
  service(Port1, b1, n1, "P1");
  static uint32_t t = 0;
  if (millis() - t > 2000) {
    t = millis();
    Port0.println("[P0] alive — WebSerial-app port (type 'help')");
    Port1.println("[P1] alive — serial-client port (type 'help')");
  }
  delay(5);
}
