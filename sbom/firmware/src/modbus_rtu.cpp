#include "diagnostic_log.h"
#include "modbus_rtu.h"
#include "modbus_tcp_server.h"
#include "svc_config.h"
#include "plc_core.h"
#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// StamPLC built-in RS-485 (PWR-485): RX=G39, TX=G0, DIR/DE=G46.
// MANUAL direction control with plain HardwareSerial — the pattern proven by the
// pool-controller (Pentair IntelliFlo) reference on this exact board. The ESP-IDF
// UART_MODE_RS485_HALF_DUPLEX auto-DE was unreliable here (left DIR in the wrong
// state -> board neither drove the bus on TX nor received on RX). DIR LOW =
// receive (default); DIR HIGH only while we drive our reply.
#define RTU_RX_PIN  39
#define RTU_TX_PIN  0
#define RTU_DIR_PIN 46
#define RTU_BUFSZ   512

static HardwareSerial RS485(1);        // UART1
static bool     s_active = false;
static uint8_t  s_id     = 1;
static uint8_t  s_buf[RTU_BUFSZ];
static size_t   s_len    = 0;
static uint32_t s_last   = 0;
static uint32_t s_gap_ms = 4;
static TaskHandle_t s_task = nullptr;
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
struct RtuStats {
    uint32_t polls = 0, rx_bytes = 0, requests = 0, replies = 0;
    uint32_t crc_rejects = 0, overflow_bytes = 0, partial_discards = 0, max_poll_gap_us = 0;
};
static RtuStats s_stats;
static uint32_t s_last_poll_us = 0;
struct StatsLock {
    StatsLock() { portENTER_CRITICAL(&s_stats_mux); }
    ~StatsLock() { portEXIT_CRITICAL(&s_stats_mux); }
};
static void modbus_rtu_poll();
static void reverse_link_test();

// Higher priority than the network worker, on core 0. Grove/I2C/PLC scan stay
// on the loop task. UART receive, DIR and every transmit have this ONE owner.
static void rtu_task(void*) {
    for (;;) {
        modbus_rtu_poll();
        if (ulTaskNotifyTake(pdTRUE, 0)) reverse_link_test();
        vTaskDelay(1);
    }
}

static uint16_t rtu_crc(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

bool modbus_rtu_active() { return s_active; }

void modbus_rtu_begin(uint8_t slave_id, uint32_t baud) {
    if (s_active || s_task) return;
    if (!baud) { Serial.println("[PROTO] RTU refused: zero baud"); return; }
    s_id = slave_id ? slave_id : 1;
    pinMode(RTU_DIR_PIN, OUTPUT);
    digitalWrite(RTU_DIR_PIN, LOW);                 // receive by default
    RS485.begin(baud, SERIAL_8N1, RTU_RX_PIN, RTU_TX_PIN);
    uint32_t g = (uint32_t)((35UL * 11UL * 1000UL) / (baud * 10UL));   // ~3.5 char times (ms)
    s_gap_ms = g < 2 ? 2 : g;
    s_len = 0; s_last = millis(); s_last_poll_us = micros();
    s_active = true;
    if (xTaskCreatePinnedToCore(rtu_task, "plc_rtu", 4096, nullptr, 7, &s_task, 0) != pdPASS) {
        s_active = false; s_task = nullptr; RS485.end();
        digitalWrite(RTU_DIR_PIN, LOW);
        Serial.println("[PROTO] RTU worker failed; UART inactive");
        return;
    }
    Serial.printf("[PROTO] Modbus RTU slave id=%u on RS-485 @%lu 8N1 (manual DIR G46; gap %lums; dedicated worker)\n",
                  s_id, (unsigned long)baud, (unsigned long)s_gap_ms);
}

// Drive the bus for one reply, then return to receive.
static void rs485_send(const uint8_t* d, size_t n) {
    digitalWrite(RTU_DIR_PIN, HIGH);                // enable transmitter
    delayMicroseconds(50);                          // transceiver turn-around
    RS485.write(d, n);
    RS485.flush();                                  // wait for TX complete (FIFO + shift reg)
    delayMicroseconds(100);                         // margin for the final stop bit
    digitalWrite(RTU_DIR_PIN, LOW);                 // back to receive
}

void modbus_rtu_test_tx() {
    if (!s_active || !s_task) { Serial.println("[RS485TX] RTU not active (enable modbus-rtu)"); return; }
    xTaskNotifyGive(s_task);
    Serial.println("[RS485TX] queued for RTU owner");
}

void modbus_rtu_diag() {
    RtuStats snapshot;
    { StatsLock lock; snapshot = s_stats; }
    Serial.printf("{\"rtu_active\":%u,\"polls\":%lu,\"rx_bytes\":%lu,\"requests\":%lu,\"replies\":%lu,\"crc_rejects\":%lu,\"overflow_bytes\":%lu,\"partial_discards\":%lu,\"max_poll_gap_us\":%lu}\n",
        s_active ? 1u : 0u, (unsigned long)snapshot.polls,
        (unsigned long)snapshot.rx_bytes, (unsigned long)snapshot.requests,
        (unsigned long)snapshot.replies, (unsigned long)snapshot.crc_rejects,
        (unsigned long)snapshot.overflow_bytes, (unsigned long)snapshot.partial_discards,
        (unsigned long)snapshot.max_poll_gap_us);
}

static void reverse_link_test() {
    uint8_t f[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x04, 0, 0};
    uint16_t c = rtu_crc(f, 6); f[6] = c & 0xFF; f[7] = c >> 8;
    rs485_send(f, 8);
    diagnostic_log_printf(diagnostics::Origin::Bus,"[RS485TX] sent: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
}

// --- Robust frame extraction ------------------------------------------------
// A slave must recover the request even when the main loop stalls (network,
// display, USB-CDC backpressure) long enough that several master frames pile up
// in the UART driver buffer before we drain it. Plain 3.5-char gap framing then
// CRC-fails the merged blob and the slave never replies. So instead of assuming
// the whole buffer is exactly one frame, we walk it: predict each request's
// length from its function code, CRC-check at that boundary, serve it, advance.
// Unresolved bytes are skipped to resync within this buffered burst. A partial
// tail at an observed inter-frame gap is discarded, never glued to a later frame.

// Modbus RTU request length from the function code (addr+fc+...+crc16). 0 = need more.
static size_t rtu_req_len(const uint8_t* p, size_t avail) {
    if (avail < 2) return 0;                          // need the function code
    switch (p[1]) {
        case 0x01: case 0x02: case 0x03: case 0x04:   // read coils/inputs/regs
        case 0x05: case 0x06:                         // write single coil/reg
            return 8;
        case 0x0F: case 0x10:                         // write multiple coils/regs
            if (avail < 7) return 0;                  // need the byte-count field
            return 9 + (size_t)p[6];                  // ..+bytecount data +crc
        case 0x16: return 10;                         // mask write register
        case 0x17:                                    // read/write multiple regs
            if (avail < 11) return 0;
            return 13 + (size_t)p[10];
        default:  return 8;                           // assume shortest; CRC rejects if wrong
    }
}

// Serve one validated request frame (addr..data, no CRC) through the shared
// Modbus core, then drive the reply back onto the bus.
static void handle_request(const uint8_t* frame, size_t flen) {
    uint8_t addr = frame[0];
    { StatsLock lock; s_stats.requests++; }
    if (g_bustrace) plc_bus_log(addr, 'R', "Modbus RTU request (RS-485)", frame[1], true);
    if (addr == 0x00) return;                          // broadcasts ignored by this teaching implementation

    uint16_t pdu_len = (uint16_t)(flen - 3);           // strip addr + crc16
    if (pdu_len < 1 || pdu_len > 253) return;

    uint8_t mbap[260];
    mbap[0] = 0; mbap[1] = 0; mbap[2] = 0; mbap[3] = 0;
    uint16_t mlen = (uint16_t)(1 + pdu_len);
    mbap[4] = (uint8_t)(mlen >> 8); mbap[5] = (uint8_t)(mlen & 0xFF);
    mbap[6] = addr;
    memcpy(mbap + 7, frame + 1, pdu_len);

    uint8_t  resp[300];
    uint16_t resp_len = 0;
    if (modbus_server.handle_frame(mbap, (uint16_t)(7 + pdu_len), resp, sizeof(resp), resp_len, "rs485", s_id) && resp_len > 7) {
        uint16_t rpdu = (uint16_t)(resp_len - 7);
        uint8_t  out[300];
        out[0] = addr;
        memcpy(out + 1, resp + 7, rpdu);
        uint16_t cc = rtu_crc(out, (size_t)(1 + rpdu));
        out[1 + rpdu] = (uint8_t)(cc & 0xFF);
        out[2 + rpdu] = (uint8_t)(cc >> 8);
        rs485_send(out, 3 + rpdu);
        { StatsLock lock; s_stats.replies++; }
    }
}

// Walk the receive buffer, serving every complete valid frame; keep any tail.
static void process_buffer() {
    size_t start = 0;
    while (start + 4 <= s_len) {                        // 4 = shortest possible frame
        size_t flen = rtu_req_len(s_buf + start, s_len - start);
        if (flen == 0 || start + flen > s_len) break;  // incomplete -> keep tail
        if (flen < 4) { start++; continue; }
        uint16_t got = (uint16_t)s_buf[start + flen - 2] | ((uint16_t)s_buf[start + flen - 1] << 8);
        if (rtu_crc(s_buf + start, flen - 2) == got) {
            uint8_t addr = s_buf[start];
            if (addr == s_id || addr == 0x00) handle_request(s_buf + start, flen);
            start += flen;                             // consumed a whole frame
        } else {
            { StatsLock lock; s_stats.crc_rejects++; }
            start++;                                   // resync on the next byte
        }
    }
    if (start >= s_len)      s_len = 0;
    else if (start > 0)    { memmove(s_buf, s_buf + start, s_len - start); s_len -= start; }
}

static void modbus_rtu_poll() {
    if (!s_active) return;
    uint32_t poll_us = micros();
    { StatsLock lock;
      uint32_t gap = poll_us - s_last_poll_us;
      if (gap > s_stats.max_poll_gap_us) s_stats.max_poll_gap_us = gap;
      s_stats.polls++;
    }
    s_last_poll_us = poll_us;
    uint32_t now = millis();
    // A continuously transmitting peer must not monopolize a high-priority task.
    for (size_t drained = 0; drained < RTU_BUFSZ && RS485.available(); drained++) {
        uint8_t b = (uint8_t)RS485.read();
        { StatsLock lock; s_stats.rx_bytes++; }
        if (s_len < sizeof(s_buf)) { s_buf[s_len++] = b; s_last = now; }
        else { StatsLock lock; s_stats.overflow_bytes++; }
    }
    if (s_len && (now - s_last) >= s_gap_ms) {
        if (g_bustrace) {
            char bytes[49]{};
            for(size_t i=0;i<s_len && i<16;++i)snprintf(bytes+3*i,4," %02X",s_buf[i]);
            diagnostic_log_printf(diagnostics::Origin::Bus,"[BUS] RS485 rx %u:%s; first 16 bytes at most\n",unsigned(s_len),bytes);
        }
        process_buffer();
        if (s_len) {
            { StatsLock lock; s_stats.partial_discards++; }
            s_len = 0;
        }
    }
    if (s_len >= sizeof(s_buf)) {
        { StatsLock lock; s_stats.overflow_bytes += s_len; }
        s_len = 0;
    }
}
