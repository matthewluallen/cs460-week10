#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "chardev/char-fe.h"
#include "qemu/fifo8.h"

#define TYPE_ESP32C3_JTAG "misc.esp32c3.usb_serial_jtag"
#define ESP32C3_JTAG(obj) OBJECT_CHECK(ESP32C3UsbJtagState, (obj), TYPE_ESP32C3_JTAG)

#define ESP32C3_JTAG_REGS_SIZE (0x84)

/* USB-Serial-JTAG register offsets (S3/C3/C6 share this layout) */
#define USB_SERIAL_JTAG_EP1_REG                 0x00  /* RDWR_BYTE: TX/RX FIFO byte */
#define USB_SERIAL_JTAG_EP1_CONF_REG            0x04
#define USB_SERIAL_JTAG_INT_RAW_REG             0x08
#define USB_SERIAL_JTAG_INT_ST_REG              0x0C
#define USB_SERIAL_JTAG_INT_ENA_REG             0x10
#define USB_SERIAL_JTAG_INT_CLR_REG             0x14

/* EP1_CONF bits */
#define USB_SERIAL_JTAG_WR_DONE                  (1u << 0)  /* flush: commit IN pkt */
#define USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE   (1u << 1)  /* TX FIFO has space   */
#define USB_SERIAL_JTAG_SERIAL_OUT_EP_DATA_AVAIL (1u << 2)  /* RX byte available   */

/* Interrupt bits (INT_RAW/ST/ENA/CLR) */
#define USB_SERIAL_JTAG_SOF_INT                  (1u << 1)
#define USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT  (1u << 2)  /* host->device data   */
#define USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT      (1u << 3)  /* TX FIFO drained     */
#define USB_SERIAL_JTAG_BUS_RESET_INT            (1u << 9)

#define USB_SERIAL_JTAG_RX_FIFO_LEN 64

typedef struct ESP32C3UsbJtagState {
    SysBusDevice parent_object;
    MemoryRegion iomem;
    CharBackend  chr;       /* backs the USB-CDC console (host <-> device) */
    qemu_irq     irq;       /* ETS_USB_SERIAL_JTAG_INTR_SOURCE            */
    Fifo8        tx_fifo;   /* committed packet retained until host accepts it */
    bool         tx_pending;
    guint        tx_watch;
    Fifo8        rx_fifo;   /* host -> device bytes (empty unless injected) */
    uint32_t     int_ena;   /* INT_ENA latch                              */
    uint32_t     int_raw;   /* latched edge bits (BUS_RESET/SOF); level bits computed */
} ESP32C3UsbJtagState;
