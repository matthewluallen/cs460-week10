#include "modbus_serial.h"
#include "modbus_tcp_server.h"
#include <string.h>
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
#include <hal/usb_serial_jtag_ll.h>
#endif

static bool   s_active = false;
static uint8_t s_buf[300];
static size_t  s_len = 0;
static uint8_t s_tx[ModbusTCPServer::MAX_ADU];
static uint16_t s_tx_len = 0, s_tx_pos = 0;
static uint32_t s_tx_started = 0;
static bool s_faulted = false;

// HWCDC's TX ring is 256 bytes, smaller than a maximum Modbus reply. Never
// call its blocking write/flush path with more than currently free capacity:
// a timeout can truncate a frame and insert the driver's warning into it.
// Retain response ownership across loop visits; never replay a committed write.
static void transmit_pending() {
    if (!s_tx_len) return;
    if ((uint32_t)(millis() - s_tx_started) >= 2000) {
        s_faulted = true; s_tx_len = 0; s_len = 0;
        return; // Failed session stays silent until gateway entry/reset.
    }
    int free = Serial.availableForWrite();
    if (free <= 0) return;
    size_t n = s_tx_len - s_tx_pos;
    if (n > 64) n = 64;
    if (n > (size_t)free) n = (size_t)free;
    size_t sent = Serial.write(s_tx + s_tx_pos, n);
    if (sent != n) {
        s_faulted = true; s_tx_len = 0; s_len = 0;
        return;
    }
    s_tx_pos += sent;
    if (s_tx_pos == s_tx_len) s_tx_len = s_tx_pos = 0;
}

bool modbus_serial_active() { return s_active; }

void modbus_serial_enter() {
    s_active = true;
    s_len = 0;
    s_tx_len = s_tx_pos = 0;
    s_faulted = false;
}

void modbus_serial_poll() {
    if (!s_active || s_faulted) return;
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
    // HWCDC can leave a queued short packet asleep after the initial write's
    // empty-FIFO interrupt races the ISR's interrupt disable. Re-arm delivery
    // without its blocking flush (which drops queued data after a 1ms timeout).
    // This submits no bytes and never repeats a Modbus write.
    usb_serial_jtag_ll_txfifo_flush();
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
#endif
    transmit_pending();
    if (s_faulted || s_tx_len) return;
    // drain the USB serial into the frame buffer
    while (Serial.available() > 0 && s_len < sizeof(s_buf)) {
        s_buf[s_len++] = (uint8_t)Serial.read();
    }
    // parse complete MBAP frames: [txn(2)][proto(2)][len(2)][unit(1)][PDU...]
    // len = count of bytes after the length field (unit + PDU); total = 6 + len.
    while (s_len >= 7) {
        uint16_t length = ((uint16_t)s_buf[4] << 8) | s_buf[5];
        size_t frame = 6u + length;
        // sanity: protocol id must be 0; length in range. Otherwise resync by 1 byte.
        if (s_buf[2] != 0 || s_buf[3] != 0 || length < 2 || length > ModbusTCPServer::MAX_ADU - 6) {
            memmove(s_buf, s_buf + 1, --s_len);
            continue;
        }
        if (s_len < frame) break;                 // wait for the rest of the frame
        s_tx_pos = 0;
        modbus_server.handle_frame(s_buf, frame, s_tx, sizeof(s_tx), s_tx_len, "usb-serial", 1, PlcAccessPeer::serial());
        size_t rem = s_len - frame;
        if (rem) memmove(s_buf, s_buf + frame, rem);
        s_len = rem;
        if (s_tx_len) {
            s_tx_started = millis();
            transmit_pending();
            return; // One response owns the TX buffer, including a coalesced tail.
        }
    }
    // buffer overflow with no valid frame -> drop the oldest half to recover
    if (s_len >= sizeof(s_buf)) { memmove(s_buf, s_buf + sizeof(s_buf)/2, sizeof(s_buf)/2); s_len = sizeof(s_buf)/2; }
}
