/*
 * ESP32-C3 / S3 USB Serial JTAG emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * TBD twin: fleshed out from a pure stub into a working USB-CDC console.
 * On the real StamPLC the USB-C port is the primary (and, in the default
 * "contained" config, the ONLY) operator interface: banner, config menu,
 * live PLC console, and the PLCLOAD program-upload channel all ride on it.
 * The Arduino core exposes it as HWCDC "Serial", whose TX is INTERRUPT
 * driven: write() fills a ring buffer and relies on the SERIAL_IN_EMPTY
 * interrupt to flush bytes to EP1_REG.  A pure stub, or a model that never
 * raises IN_EMPTY, strands every Serial.print in the ring (silent console);
 * a model that holds IN_EMPTY as a permanent level storms the early IDF
 * console into a hang ("Wall 7").  The correct behaviour is an EDGE:
 * IN_EMPTY latches when a packet is flushed (WR_DONE) or when the firmware
 * enables the interrupt while the IN endpoint is already empty, and is
 * cleared by INT_CLR.  RX is fed only by a real host on the chardev, so the
 * receive path is empty at rest and the firmware's program-loader poll
 * (`while (Serial.available())`) returns immediately instead of spinning.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/misc/esp32c3_jtag.h"


/* Raise the CPU line when an enabled interrupt condition is pending.
 *
 * int_raw holds LATCHED edge bits (BUS_RESET/SOF and, crucially, IN_EMPTY),
 * which INT_CLR can clear and which do NOT re-assert on their own.  The only
 * live/level contribution is RECV_PKT, which tracks whether a real host has
 * queued RX bytes.  Because IN_EMPTY is a normal latched bit here (set only
 * on flush/enable, cleared by INT_CLR) the early IDF console can quiesce it
 * instead of being re-interrupted forever. */
static void esp32c3_jtag_update_irq(ESP32C3UsbJtagState *s)
{
    uint32_t deliver = (s->int_raw & s->int_ena);
    if (!fifo8_is_empty(&s->rx_fifo)) {
        deliver |= (USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT & s->int_ena);
    }
    qemu_set_irq(s->irq, !!deliver);
}

/* Model the endpoint packet rather than ignoring chardev short writes. This
 * keeps backpressure in the emulated peripheral; never block the QEMU thread. */
static void esp32c3_jtag_drain_tx(ESP32C3UsbJtagState *s);
static gboolean esp32c3_jtag_tx_ready(void *channel, GIOCondition cond, void *opaque)
{
    ESP32C3UsbJtagState *s = opaque;
    s->tx_watch = 0;
    if (!(cond & G_IO_HUP)) esp32c3_jtag_drain_tx(s);
    return G_SOURCE_REMOVE;
}
static void esp32c3_jtag_drain_tx(ESP32C3UsbJtagState *s)
{
    if (!s->tx_pending) return;
    if (!fifo8_is_empty(&s->tx_fifo)) {
        uint8_t packet[64];
        uint32_t n = fifo8_peek_buf(&s->tx_fifo, packet, fifo8_num_used(&s->tx_fifo));
        int sent = qemu_chr_fe_backend_connected(&s->chr) ?
                   qemu_chr_fe_write(&s->chr, packet, n) : 0;
        if (sent > 0) fifo8_drop(&s->tx_fifo, sent);
    }
    if (fifo8_is_empty(&s->tx_fifo)) {
        s->tx_pending = false;
        s->int_raw |= USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT;
        esp32c3_jtag_update_irq(s);
    } else if (!s->tx_watch && qemu_chr_fe_backend_connected(&s->chr)) {
        s->tx_watch = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                          esp32c3_jtag_tx_ready, s);
    }
}
static void esp32c3_jtag_host_event(void *opaque, QEMUChrEvent event)
{
    ESP32C3UsbJtagState *s = opaque;
    if (event == CHR_EVENT_OPENED) esp32c3_jtag_drain_tx(s);
    if (event == CHR_EVENT_CLOSED && s->tx_watch) {
        g_source_remove(s->tx_watch); s->tx_watch = 0;
    }
}

static uint64_t esp32c3_jtag_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(opaque);
    uint32_t r = 0;

    switch (addr) {
    case USB_SERIAL_JTAG_EP1_REG:
        /* Pop one RX byte; empty reads as 0 (real hardware returns stale FIFO). */
        if (!fifo8_is_empty(&s->rx_fifo)) {
            r = fifo8_pop(&s->rx_fifo);
            esp32c3_jtag_update_irq(s);
        }
        break;
    case USB_SERIAL_JTAG_EP1_CONF_REG:
        /* A committed packet owns the endpoint until the host accepts it. */
        if (!s->tx_pending && !fifo8_is_full(&s->tx_fifo)) {
            r = USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE;
        }
        if (!fifo8_is_empty(&s->rx_fifo)) {
            r |= USB_SERIAL_JTAG_SERIAL_OUT_EP_DATA_AVAIL;
        }
        break;
    case USB_SERIAL_JTAG_INT_RAW_REG:
        /* Latched edges (incl. IN_EMPTY) + live RECV_PKT for real RX. */
        r = s->int_raw;
        if (!fifo8_is_empty(&s->rx_fifo)) {
            r |= USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT;
        }
        /* TBD twin: present an always-attached USB host.  IDF's connection
         * monitor (usb_serial_jtag_connection_monitor.c) polls the SOF raw bit
         * from a FreeRTOS tick hook: a real host emits a Start-of-Frame every
         * 1ms, so the bit is set every tick and usb_serial_jtag_is_connected()
         * stays true.  Without it the port latches "disconnected" after ~3ms
         * and HWCDC (Serial) drops every byte with setTxTimeoutMs(0) — the
         * StamPLC console is silent.  We report SOF live in the RAW read only
         * (never stored in int_raw, never masked into delivery), so it can
         * never raise a CPU interrupt or storm the ISR — it only tells the
         * firmware "a host is plugged into the USB-C port." */
        r |= USB_SERIAL_JTAG_SOF_INT;
        break;
    case USB_SERIAL_JTAG_INT_ST_REG:
        r = s->int_raw;
        if (!fifo8_is_empty(&s->rx_fifo)) {
            r |= USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT;
        }
        r &= s->int_ena;
        break;
    case USB_SERIAL_JTAG_INT_ENA_REG:
        r = s->int_ena;
        break;
    default:
        break;
    }
    return r;
}

static void esp32c3_jtag_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(opaque);

    switch (addr) {
    case USB_SERIAL_JTAG_EP1_REG:
        if (!s->tx_pending && !fifo8_is_full(&s->tx_fifo)) {
            fifo8_push(&s->tx_fifo, value & 0xff);
        }
        break;
    case USB_SERIAL_JTAG_EP1_CONF_REG:
        if (value & USB_SERIAL_JTAG_WR_DONE) {
            s->tx_pending = true;
            esp32c3_jtag_drain_tx(s);
        }
        break;
    case USB_SERIAL_JTAG_INT_ENA_REG:
        s->int_ena = value;
        /* Enabling IN_EMPTY while the IN endpoint is already empty (our steady
         * state) must fire the ISR so it starts draining the TX ring — model
         * the "enable when condition already true" edge. */
        if ((value & USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT) &&
            !s->tx_pending && fifo8_is_empty(&s->tx_fifo)) {
            s->int_raw |= USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT;
        }
        esp32c3_jtag_update_irq(s);
        break;
    case USB_SERIAL_JTAG_INT_CLR_REG:
        s->int_raw &= ~(uint32_t)value;              /* clear latched edge bits */
        esp32c3_jtag_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps esp32c3_jtag_ops = {
    .read = esp32c3_jtag_read,
    .write = esp32c3_jtag_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* chardev -> device: a real host typed something into the console. */
static int esp32c3_jtag_can_rx(void *opaque)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(opaque);
    return fifo8_num_free(&s->rx_fifo);
}

static void esp32c3_jtag_rx(void *opaque, const uint8_t *buf, int size)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(opaque);
    for (int i = 0; i < size && !fifo8_is_full(&s->rx_fifo); i++) {
        fifo8_push(&s->rx_fifo, buf[i]);
    }
    esp32c3_jtag_update_irq(s);                      /* asserts RECV_PKT */
}

static void esp32c3_jtag_reset_hold(Object *obj, ResetType type)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(obj);
    if (s->tx_watch) { g_source_remove(s->tx_watch); s->tx_watch = 0; }
    fifo8_reset(&s->tx_fifo); s->tx_pending = false;
    fifo8_reset(&s->rx_fifo);
    s->int_ena = 0;
    s->int_raw = 0;
    qemu_set_irq(s->irq, 0);
}

static void esp32c3_jtag_realize(DeviceState *dev, Error **errp)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(dev);
    qemu_chr_fe_set_handlers(&s->chr, esp32c3_jtag_can_rx, esp32c3_jtag_rx,
                             esp32c3_jtag_host_event, NULL, s, NULL, true);
}

static void esp32c3_jtag_init(Object *obj)
{
    ESP32C3UsbJtagState *s = ESP32C3_JTAG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    fifo8_create(&s->tx_fifo, 64);
    fifo8_create(&s->rx_fifo, USB_SERIAL_JTAG_RX_FIFO_LEN);
    memory_region_init_io(&s->iomem, obj, &esp32c3_jtag_ops, s,
                          TYPE_ESP32C3_JTAG, ESP32C3_JTAG_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property esp32c3_jtag_properties[] = {
    DEFINE_PROP_CHR("chardev", ESP32C3UsbJtagState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32c3_jtag_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32c3_jtag_reset_hold;
    dc->realize = esp32c3_jtag_realize;
    device_class_set_props(dc, esp32c3_jtag_properties);
}

static const TypeInfo esp32c3_jtag_info = {
    .name = TYPE_ESP32C3_JTAG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C3UsbJtagState),
    .instance_init = esp32c3_jtag_init,
    .class_init = esp32c3_jtag_class_init
};

static void esp32c3_jtag_types(void)
{
    type_register_static(&esp32c3_jtag_info);
}

type_init(esp32c3_jtag_types)
