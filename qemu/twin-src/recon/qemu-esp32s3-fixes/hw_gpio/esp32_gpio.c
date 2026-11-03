/*
 * ESP32 GPIO emulation
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/i2c/tbd_bridge.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/esp32_gpio.h"



static void hx711_poll(void);   /* TBD twin: HX711 readiness poll (defined below) */

static uint64_t esp32_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32GpioState *s = ESP32_GPIO(opaque);
    uint64_t r = 0;
    switch (addr) {
    case A_GPIO_STRAP:
        r = s->strap_mode;
        break;
    case A_GPIO_OUT:
        r = s->gpio_out;
        break;
    case A_GPIO_ENABLE:
        r = s->gpio_enable;
        break;
    case A_GPIO_IN:
        /* TBD twin: input level register. Pins default low (as before this
         * case existed); bit-bang peripherals such as the HX711 load cell drive
         * individual bits via s->gpio_in so digitalRead()/gpio_get_level() work.
         * Poll the HX711 here too, so a fresh sample becomes ready even inside a
         * tight guest busy-wait on DOUT (independent of timer-callback timing). */
        hx711_poll();
        r = s->gpio_in;
        break;

    default:
        break;
    }
    return r;
}

/* TBD twin: the W5500 model (in esp32s3.c) needs the chip-select edge on
 * GPIO11 to delimit SPI frames. Defined in esp32s3.c (same machine binary). */


/* ======================================================================== */
/*  TBD twin: Grove HX711 24-bit load-cell ADC (BLUE Grove port)          */
/* ------------------------------------------------------------------------ */
/*  DOUT = GPIO4, PD_SCK = GPIO5: the observed physical student-kit wiring. */
/*  Signature the firmware's grove_units.cpp::hx711_try() checks for:         */
/*    1. DOUT idles HIGH, then falls LOW ("data ready") within 500 ms;        */
/*    2. 24 data bits shift out MSB-first, one per PD_SCK rising edge, read    */
/*       by the firmware while SCK is HIGH;                                    */
/*    3. after the 25th (gain=128/chA) pulse DOUT returns HIGH.               */
/*  A virtual-clock timer makes a fresh sample ready ~80x/s; the sample is the */
/*  process tank weight (rises while the pump runs) via tbd_twin_weight_raw. */
#define HX711_DOUT_PIN 4
#define HX711_SCK_PIN  5


static struct {
    Esp32GpioState *gpio;
    QEMUTimer *timer;
    int      ready;      /* a conversion result is available (DOUT pulled low)   */
    int      reading;    /* a PD_SCK pulse train is in progress                   */
    int      bit;        /* rising-edge count within the current read (1..25)     */
    uint32_t sample;     /* 24-bit value being shifted out, MSB-first             */
    int      last_sck;   /* previous PD_SCK level (to detect edges)               */
    int64_t  next_ready_ms; /* virtual-clock time the next sample becomes ready    */
    int64_t  last_edge_ms;  /* virtual-clock time of the last PD_SCK edge          */
} s_hx;

/* A real HX711 read burst is 25 pulses only microseconds apart, framed by long
 * (millisecond) idle gaps. Any PD_SCK activity separated from the burst by more
 * than this is a different frame — used to abandon a partial/stray frame so a
 * clean read always resynchronises (stray GPIO5 toggles at boot can otherwise
 * leave the shift state stuck mid-frame). */
#define HX711_FRAME_GAP_MS 2

static void hx711_set_dout(int level)
{
    if (!s_hx.gpio) return;
    if (level) s_hx.gpio->gpio_in |=  (1u << HX711_DOUT_PIN);
    else       s_hx.gpio->gpio_in &= ~(1u << HX711_DOUT_PIN);
}

/* Arm a fresh conversion (DOUT low = "data ready") once the inter-sample gap has
 * elapsed and no read is in progress. Driven both by a timer and lazily by
 * GPIO_IN reads, so readiness advances even inside a guest busy-wait on DOUT. */
static void hx711_poll(void)
{
    if (!s_hx.gpio) return;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    /* Recover from a partial/abandoned frame: mid-read but no clock edge for a
     * while (e.g. stray boot-time GPIO5 toggles left us stuck). Reset and re-arm. */
    if (s_hx.reading && now - s_hx.last_edge_ms > HX711_FRAME_GAP_MS) {
        s_hx.reading = 0; s_hx.bit = 0; s_hx.ready = 0;
        s_hx.next_ready_ms = now;          /* arm immediately below */
    }
    if (s_hx.reading || s_hx.ready) return;
    if (now < s_hx.next_ready_ms) return;
    s_hx.sample = (uint32_t)tbd_twin_weight_raw() & 0xFFFFFF;
    s_hx.bit = 0;
    s_hx.ready = 1;
    hx711_set_dout(0);                     /* DATA READY: DOUT low */
}

static void hx711_timer(void *opaque)
{
    hx711_poll();                          /* ~80 samples/s */
    timer_mod(s_hx.timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 12);
}

/* Called from the GPIO write hook on any PD_SCK (GPIO5) transition. */
static void hx711_sck_notify(int level)
{
    if (!s_hx.gpio) return;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    if (level && !s_hx.last_sck) {                 /* rising edge */
        /* A rising edge long after the previous one starts a new frame; drop any
         * stale partial frame so the shift always resynchronises to bit 0. */
        if (now - s_hx.last_edge_ms > HX711_FRAME_GAP_MS) {
            s_hx.reading = 0;
            s_hx.bit = 0;
            if (!s_hx.ready) {                     /* firmware only clocks when
                                                    * DOUT is low, but be safe */
                s_hx.sample = (uint32_t)tbd_twin_weight_raw() & 0xFFFFFF;
                s_hx.ready = 1;
            }
        }
        if (s_hx.ready || s_hx.reading) {
            s_hx.reading = 1;
            s_hx.bit++;
            if (s_hx.bit >= 1 && s_hx.bit <= 24) { /* present data bit, MSB first */
                hx711_set_dout((s_hx.sample >> (24 - s_hx.bit)) & 1);
            } else {                               /* 25th (gain) pulse: end frame */
                hx711_set_dout(1);                 /* DOUT returns HIGH */
            }
        }
    } else if (!level && s_hx.last_sck) {          /* falling edge */
        if (s_hx.reading && s_hx.bit >= 25) {      /* pulse train complete */
            s_hx.reading = 0;
            s_hx.ready   = 0;
            s_hx.bit     = 0;
            hx711_set_dout(1);                     /* idle HIGH until next sample */
            s_hx.next_ready_ms = now + 12;
        }
    }
    s_hx.last_edge_ms = now;
    s_hx.last_sck = level;
}

void tbd_hx711_init(Esp32GpioState *gpio)
{
    const char *profile = getenv("TBD_PORT_C");
    if (profile && strcmp(profile, "hx711")) {
        if (strcmp(profile, "disabled") && strcmp(profile, "h2-uart")) {
            fprintf(stderr, "Invalid TBD_PORT_C profile: %s\n", profile);
            exit(1);
        }
        /* The UART2 peer is external; never also attach the HX711 to its pins. */
        fprintf(stderr, "TBD Port C model: %s (HX711 absent)\n", profile);
        return;
    }
    s_hx.gpio = gpio;
    s_hx.last_sck = 0; s_hx.ready = 0; s_hx.reading = 0; s_hx.bit = 0;
    hx711_set_dout(1);                             /* idle HIGH (not ready) */
    s_hx.next_ready_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 30;
    s_hx.timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, hx711_timer, NULL);
    timer_mod(s_hx.timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 30);
}

static void esp32_gpio_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32GpioState *s = ESP32_GPIO(opaque);
    uint32_t before = s->gpio_out;
    switch (addr) {
    case A_GPIO_OUT:         s->gpio_out = value;            break;
    case A_GPIO_OUT_W1TS:    s->gpio_out |= value;           break;
    case A_GPIO_OUT_W1TC:    s->gpio_out &= ~(uint32_t)value; break;
    case A_GPIO_ENABLE:      s->gpio_enable = value;          break;
    case A_GPIO_ENABLE_W1TS: s->gpio_enable |= value;         break;
    case A_GPIO_ENABLE_W1TC: s->gpio_enable &= ~(uint32_t)value; break;
    default: break;
    }
    uint32_t changed = before ^ s->gpio_out;
    /* Notify the W5500 SPI model on any GPIO11 (its chip-select) transition. */
    if (changed & (1u << 11)) {
        tbd_w5500_cs_notify(!(s->gpio_out & (1u << 11)));
    }
    /* Clock the HX711 load cell on any PD_SCK (GPIO5) transition. */
    if (changed & (1u << HX711_SCK_PIN)) {
        hx711_sck_notify(!!(s->gpio_out & (1u << HX711_SCK_PIN)));
    }
}

static const MemoryRegionOps uart_ops = {
    .read =  esp32_gpio_read,
    .write = esp32_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_gpio_reset_hold(Object *obj, ResetType type)
{
}

static void esp32_gpio_realize(DeviceState *dev, Error **errp)
{
}

static void esp32_gpio_init(Object *obj)
{
    Esp32GpioState *s = ESP32_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Set the default value for the strap_mode property */
    object_property_set_int(obj, "strap_mode", ESP32_STRAP_MODE_FLASH_BOOT, &error_fatal);

    memory_region_init_io(&s->iomem, obj, &uart_ops, s,
                          TYPE_ESP32_GPIO, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property esp32_gpio_properties[] = {
    /* The strap_mode needs to be explicitly set in the instance init, thus, set
     * the default value to 0. */
    DEFINE_PROP_UINT32("strap_mode", Esp32GpioState, strap_mode, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32_gpio_reset_hold;
    dc->realize = esp32_gpio_realize;
    device_class_set_props(dc, esp32_gpio_properties);
}

static const TypeInfo esp32_gpio_info = {
    .name = TYPE_ESP32_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32GpioState),
    .instance_init = esp32_gpio_init,
    .class_init = esp32_gpio_class_init,
    .class_size = sizeof(Esp32GpioClass),
};

static void esp32_gpio_register_types(void)
{
    type_register_static(&esp32_gpio_info);
}

type_init(esp32_gpio_register_types)
