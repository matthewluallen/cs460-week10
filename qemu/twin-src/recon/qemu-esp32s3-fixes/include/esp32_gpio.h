#pragma once

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/registerfields.h"

#define TYPE_ESP32_GPIO "esp32.gpio"
#define ESP32_GPIO(obj)             OBJECT_CHECK(Esp32GpioState, (obj), TYPE_ESP32_GPIO)
#define ESP32_GPIO_GET_CLASS(obj)   OBJECT_GET_CLASS(Esp32GpioClass, obj, TYPE_ESP32_GPIO)
#define ESP32_GPIO_CLASS(klass)     OBJECT_CLASS_CHECK(Esp32GpioClass, klass, TYPE_ESP32_GPIO)

REG32(GPIO_OUT,         0x0004)
REG32(GPIO_OUT_W1TS,    0x0008)
REG32(GPIO_OUT_W1TC,    0x000C)
REG32(GPIO_ENABLE,      0x0020)
REG32(GPIO_ENABLE_W1TS, 0x0024)
REG32(GPIO_ENABLE_W1TC, 0x0028)
REG32(GPIO_STRAP, 0x0038)
REG32(GPIO_IN,    0x003C)   /* TBD twin: GPIO0-31 input level register */

#define ESP32_STRAP_MODE_FLASH_BOOT 0x12
#define ESP32_STRAP_MODE_UART_BOOT  0x0f

typedef struct Esp32GpioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t strap_mode;
    uint32_t gpio_out;      /* GPIO_OUT latch (pins 0-31)    */
    uint32_t gpio_enable;   /* GPIO_ENABLE latch (pins 0-31) */
    uint32_t gpio_in;       /* TBD twin: GPIO_IN level (pins 0-31), driven by
                             * on-board bit-bang peripherals (e.g. HX711 DOUT). */
} Esp32GpioState;

typedef struct Esp32GpioClass {
    SysBusDeviceClass parent_class;
} Esp32GpioClass;

void tbd_hx711_init(Esp32GpioState *gpio);
