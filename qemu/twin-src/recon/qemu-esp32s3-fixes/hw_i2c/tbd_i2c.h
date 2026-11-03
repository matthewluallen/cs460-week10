/* tbd_i2c.h — TBD StamPLC twin: I²C device-model register cores.
 * ---------------------------------------------------------------------------
 * Pure C, ZERO QEMU dependencies, so the register logic can be unit-tested with
 * plain gcc (test/test_i2c_models.c) AND reused by the QEMU I2CSlave wrappers
 * (devices/qemu_i2c_glue.c) and, as a fallback, by the GDB-memory bridge.
 *
 * Every model synthesizes the exact register bytes the REAL firmware decodes,
 * verified against M5Stack's M5StamPLC library
 * (firmware/.pio/libdeps/m5stamplc/M5StamPLC/src). The chip set and maps are the
 * firmware's actual ones — four chips, all on the internal I²C bus:
 *
 *   AW9523B  0x59  _io_expander_b: relays P0.0-3 (out), DI on P0.4-7 + P1.12-15
 *   INA226   0x40  bus voltage (1.25 mV/LSB) + shunt current (Rshunt=0.01Ω, 2A)
 *   LM75B    0x48  temperature, temp() = (read16>>5 sign-extended) * 0.125 °C
 *   RX8130   0x32  RTC (BCD) — stub, not sensor/actuator critical
 *
 * The physics engine sets inputs through the set_* helpers; the firmware's own
 * sensor code reads them over (emulated) I²C; the firmware drives the relays and
 * the bridge reads them back through aw9523_get_relays(). That closed loop runs
 * through the actual firmware, which is the point.
 * ---------------------------------------------------------------------------
 */
#ifndef TBD_I2C_H
#define TBD_I2C_H

#include <stdint.h>
#include <stdbool.h>

/* Internal-bus 7-bit addresses (firmware ground truth). */
#define TBD_ADDR_AW9523 0x59
#define TBD_ADDR_INA226 0x40
#define TBD_ADDR_LM75B  0x48
#define TBD_ADDR_RX8130 0x32

/* ======================================================================== */
/*  AW9523B — 16-ch GPIO/LED expander (relays + digital inputs)             */
/* ======================================================================== */
/* Register map (Adafruit/AW9523 datasheet), the subset the firmware uses.   */
#define AW9523_REG_INPUT0  0x00  /* RO */
#define AW9523_REG_INPUT1  0x01  /* RO */
#define AW9523_REG_OUTPUT0 0x02  /* RW — relays live in bits 0-3            */
#define AW9523_REG_OUTPUT1 0x03  /* RW */
#define AW9523_REG_CONFIG0 0x04  /* RW — direction (1=input per datasheet)  */
#define AW9523_REG_CONFIG1 0x05  /* RW */
#define AW9523_REG_INT0    0x06  /* RW */
#define AW9523_REG_INT1    0x07  /* RW */
#define AW9523_REG_ID      0x10  /* RO — reads 0x23                          */
#define AW9523_REG_GCR     0x11  /* RW — global control (P0 push-pull/OD)    */
#define AW9523_REG_LEDMODE0 0x12 /* RW */
#define AW9523_REG_LEDMODE1 0x13 /* RW */
#define AW9523_REG_DIM_BASE 0x20 /* RW — 0x20..0x2F per-channel dimming      */
#define AW9523_REG_SOFTRESET 0x7F/* WO */
#define AW9523_CHIP_ID     0x23

typedef struct {
    uint8_t out0, out1;      /* output latches                              */
    uint8_t cfg0, cfg1;      /* direction registers                         */
    uint8_t gcr;             /* global control                              */
    uint8_t ledmode0, ledmode1;
    uint8_t int0, int1;
    uint8_t dim[16];
    uint8_t ext_di;          /* physics-driven inputs, DI0..DI7 = bits 0..7 */
} aw9523_t;

void    aw9523_reset(aw9523_t *d);
void    aw9523_write_reg(aw9523_t *d, uint8_t reg, uint8_t val);
uint8_t aw9523_read_reg(aw9523_t *d, uint8_t reg);
/* Physics <-> firmware helpers (used by the bridge / test harness): */
void    aw9523_set_di(aw9523_t *d, uint8_t di8);   /* DI0..7 as bits 0..7  */
uint8_t aw9523_get_relays(aw9523_t *d);            /* relay latch, bits 0-3 */

/* ======================================================================== */
/*  INA226 — bus voltage + shunt current (pointer-register, 16-bit BE)      */
/* ======================================================================== */
#define INA226_REG_CONFIG   0x00
#define INA226_REG_SHUNTV   0x01  /* 2.5 µV/LSB, signed                     */
#define INA226_REG_BUSV     0x02  /* 1.25 mV/LSB                            */
#define INA226_REG_POWER    0x03
#define INA226_REG_CURRENT  0x04
#define INA226_REG_CALIB    0x05
#define INA226_REG_MASKEN   0x06
#define INA226_REG_ALERTLIM 0x07
#define INA226_REG_MANUF_ID 0xFE  /* reads 0x5449 "TI"                      */
#define INA226_REG_DIE_ID   0xFF  /* reads 0x2260                           */
#define INA226_MANUF_ID     0x5449
#define INA226_DIE_ID       0x2260

/* Firmware config: Rshunt = 0.01 Ω, max_expected_current = 2.0 A.
 *   bus voltage:   getBusVoltage()   = reg(BUSV)  * 0.00125  V
 *   shunt current: getShuntCurrent() = reg(SHUNTV)* 2.5e-6 / 0.01  (shunt path)
 *                                    ≈ reg(CURRENT)* (2/32768)      (current path)
 * We populate SHUNTV, BUSV and CURRENT so whichever path the driver uses decodes
 * back to the physics value.
 */
typedef struct {
    uint16_t config, calib, mask_en, alert_lim;
    int16_t  shunt_raw;      /* SHUNTV register (signed)                    */
    uint16_t bus_raw;        /* BUSV register                               */
    int16_t  current_raw;    /* CURRENT register (signed)                   */
    uint16_t power_raw;
    uint8_t  ptr;            /* pointer register                            */
} ina226_t;

void     ina226_reset(ina226_t *d);
void     ina226_write_reg(ina226_t *d, uint8_t reg, uint16_t val);
uint16_t ina226_read_reg(ina226_t *d, uint8_t reg);
void     ina226_set_bus_mv(ina226_t *d, int32_t millivolts);
void     ina226_set_current_ma(ina226_t *d, int32_t milliamps);

/* ======================================================================== */
/*  LM75B — temperature (pointer-register, temp reg is 16-bit BE)           */
/* ======================================================================== */
#define LM75B_REG_TEMP   0x00  /* RO 16-bit: (raw>>5 sign-extended)*0.125 °C */
#define LM75B_REG_CONF   0x01  /* RW 8-bit                                   */
#define LM75B_REG_THYST  0x02  /* RW 16-bit                                  */
#define LM75B_REG_TOS    0x03  /* RW 16-bit                                  */

typedef struct {
    uint16_t temp_raw;       /* TEMP register (11-bit left-justified)       */
    uint8_t  conf;
    uint16_t thyst, tos;
    uint8_t  ptr;
} lm75b_t;

void     lm75b_reset(lm75b_t *d);
void     lm75b_write_reg(lm75b_t *d, uint8_t reg, uint16_t val);
uint16_t lm75b_read_reg(lm75b_t *d, uint8_t reg);
void     lm75b_set_temp_mc(lm75b_t *d, int32_t milli_celsius);

/* ======================================================================== */
/*  RX8130 — RTC (stub: valid BCD time so begin() succeeds)                 */
/* ======================================================================== */
#define RX8130_REG_SEC   0x10
#define RX8130_REG_MIN   0x11
#define RX8130_REG_HOUR  0x12
#define RX8130_REG_WEEK  0x13
#define RX8130_REG_DAY   0x14
#define RX8130_REG_MONTH 0x15
#define RX8130_REG_YEAR  0x16

typedef struct { uint8_t reg[0x40]; uint8_t ptr; } rx8130_t;

void    rx8130_reset(rx8130_t *d);
void    rx8130_write_reg(rx8130_t *d, uint8_t reg, uint8_t val);
uint8_t rx8130_read_reg(rx8130_t *d, uint8_t reg);

#endif /* TBD_I2C_H */
