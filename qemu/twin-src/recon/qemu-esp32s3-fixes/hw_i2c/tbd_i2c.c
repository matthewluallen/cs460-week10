/* tbd_i2c.c — TBD StamPLC twin I²C device-model register cores.
 * Pure C. See tbd_i2c.h for the firmware-verified maps and decode formulas.
 */
#include "tbd_i2c.h"

/* Round-to-nearest integer division that handles negative numerators. */
static int32_t div_round(int32_t n, int32_t d)
{
    if ((n < 0) != (d < 0)) return (n - d / 2) / d;
    return (n + d / 2) / d;
}

/* ======================================================================== */
/*  AW9523B                                                                 */
/* ======================================================================== */
void aw9523_reset(aw9523_t *d)
{
    for (int i = 0; i < 16; i++) d->dim[i] = 0;
    d->out0 = d->out1 = 0x00;
    d->cfg0 = d->cfg1 = 0xFF;      /* power-up: all inputs                  */
    d->gcr  = 0x00;
    d->ledmode0 = d->ledmode1 = 0xFF; /* GPIO mode (not LED)                */
    d->int0 = d->int1 = 0x00;
    d->ext_di = 0x00;
}

void aw9523_write_reg(aw9523_t *d, uint8_t reg, uint8_t val)
{
    if (reg >= AW9523_REG_DIM_BASE && reg <= AW9523_REG_DIM_BASE + 15) {
        d->dim[reg - AW9523_REG_DIM_BASE] = val;
        return;
    }
    switch (reg) {
    case AW9523_REG_OUTPUT0:  d->out0 = val; break;
    case AW9523_REG_OUTPUT1:  d->out1 = val; break;
    case AW9523_REG_CONFIG0:  d->cfg0 = val; break;
    case AW9523_REG_CONFIG1:  d->cfg1 = val; break;
    case AW9523_REG_INT0:     d->int0 = val; break;
    case AW9523_REG_INT1:     d->int1 = val; break;
    case AW9523_REG_GCR:      d->gcr = val; break;
    case AW9523_REG_LEDMODE0: d->ledmode0 = val; break;
    case AW9523_REG_LEDMODE1: d->ledmode1 = val; break;
    case AW9523_REG_SOFTRESET: aw9523_reset(d); break;
    default: /* INPUT0/1 and ID are read-only; ignore */ break;
    }
}

uint8_t aw9523_read_reg(aw9523_t *d, uint8_t reg)
{
    if (reg >= AW9523_REG_DIM_BASE && reg <= AW9523_REG_DIM_BASE + 15)
        return d->dim[reg - AW9523_REG_DIM_BASE];
    switch (reg) {
    /* Input Port 0: relay latch reads back on pins 0-3 (push-pull outputs);
     * DI0-3 arrive on pins 4-7. */
    case AW9523_REG_INPUT0:
        return (uint8_t)((d->out0 & 0x0F) | ((d->ext_di & 0x0F) << 4));
    /* Input Port 1: DI4-7 arrive on pins 12-15 (bits 4-7). */
    case AW9523_REG_INPUT1:
        return (uint8_t)(((d->ext_di >> 4) & 0x0F) << 4);
    case AW9523_REG_OUTPUT0:  return d->out0;
    case AW9523_REG_OUTPUT1:  return d->out1;
    case AW9523_REG_CONFIG0:  return d->cfg0;
    case AW9523_REG_CONFIG1:  return d->cfg1;
    case AW9523_REG_INT0:     return d->int0;
    case AW9523_REG_INT1:     return d->int1;
    case AW9523_REG_ID:       return AW9523_CHIP_ID;   /* 0x23 */
    case AW9523_REG_GCR:      return d->gcr;
    case AW9523_REG_LEDMODE0: return d->ledmode0;
    case AW9523_REG_LEDMODE1: return d->ledmode1;
    default: return 0x00;
    }
}

void aw9523_set_di(aw9523_t *d, uint8_t di8) { d->ext_di = di8; }
uint8_t aw9523_get_relays(aw9523_t *d)       { return (uint8_t)(d->out0 & 0x0F); }

/* ======================================================================== */
/*  INA226                                                                  */
/* ======================================================================== */
void ina226_reset(ina226_t *d)
{
    d->config = 0x4127;    /* datasheet reset default                       */
    d->calib = 0; d->mask_en = 0; d->alert_lim = 0;
    d->shunt_raw = 0; d->bus_raw = 0; d->current_raw = 0; d->power_raw = 0;
    d->ptr = 0;
}

void ina226_write_reg(ina226_t *d, uint8_t reg, uint16_t val)
{
    switch (reg) {
    case INA226_REG_CONFIG:   d->config = val; break;
    case INA226_REG_CALIB:    d->calib = val; break;
    case INA226_REG_MASKEN:   d->mask_en = val; break;
    case INA226_REG_ALERTLIM: d->alert_lim = val; break;
    default: /* measurement + ID registers are read-only */ break;
    }
}

uint16_t ina226_read_reg(ina226_t *d, uint8_t reg)
{
    switch (reg) {
    case INA226_REG_CONFIG:   return d->config;
    case INA226_REG_SHUNTV:   return (uint16_t)d->shunt_raw;
    case INA226_REG_BUSV:     return d->bus_raw;
    case INA226_REG_POWER:    return d->power_raw;
    case INA226_REG_CURRENT:  return (uint16_t)d->current_raw;
    case INA226_REG_CALIB:    return d->calib;
    case INA226_REG_MASKEN:   return d->mask_en;
    case INA226_REG_ALERTLIM: return d->alert_lim;
    case INA226_REG_MANUF_ID: return INA226_MANUF_ID; /* 0x5449 */
    case INA226_REG_DIE_ID:   return INA226_DIE_ID;   /* 0x2260 */
    default: return 0x0000;
    }
}

/* Bus voltage: reg = mV / 1.25 = mV * 0.8. Decode: reg * 0.00125 V. */
void ina226_set_bus_mv(ina226_t *d, int32_t millivolts)
{
    if (millivolts < 0) millivolts = 0;
    int32_t raw = div_round(millivolts * 4, 5);
    if (raw > 0xFFFF) raw = 0xFFFF;
    d->bus_raw = (uint16_t)raw;
}

/* Current, both decode paths (Rshunt=0.01Ω, currentLSB=2A/32768):
 *   shunt path:   SHUNTV(2.5µV) = I·R / 2.5µV = mA·0.01 / 2.5e-3(mV·..) → reg = mA*4
 *   current path: CURRENT = mA / (2000/32768 mA) = mA * 2048/125 = mA*16.384
 */
void ina226_set_current_ma(ina226_t *d, int32_t milliamps)
{
    int32_t sv = milliamps * 4;                        /* mA * 4            */
    if (sv > 32767) sv = 32767;
    if (sv < -32768) sv = -32768;
    d->shunt_raw = (int16_t)sv;

    int32_t cr = div_round(milliamps * 2048, 125);     /* mA * 16.384       */
    if (cr > 32767) cr = 32767;
    if (cr < -32768) cr = -32768;
    d->current_raw = (int16_t)cr;

    /* Power register ≈ |current|·busV / 25 (powerLSB = 25·currentLSB); coarse. */
    int32_t p = (d->bus_raw ? (int32_t)((int64_t)(cr < 0 ? -cr : cr) * d->bus_raw / 2000) : 0);
    d->power_raw = (uint16_t)(p > 0xFFFF ? 0xFFFF : p);
}

/* ======================================================================== */
/*  LM75B                                                                   */
/* ======================================================================== */
void lm75b_reset(lm75b_t *d)
{
    d->temp_raw = 0x0000;   /* 0 °C */
    d->conf = 0x00;
    d->thyst = 0x4B00;      /* 75 °C default */
    d->tos   = 0x5000;      /* 80 °C default */
    d->ptr = 0;
}

void lm75b_write_reg(lm75b_t *d, uint8_t reg, uint16_t val)
{
    switch (reg) {
    case LM75B_REG_CONF:  d->conf = (uint8_t)val; break;
    case LM75B_REG_THYST: d->thyst = val; break;
    case LM75B_REG_TOS:   d->tos = val; break;
    default: /* TEMP is read-only */ break;
    }
}

uint16_t lm75b_read_reg(lm75b_t *d, uint8_t reg)
{
    switch (reg) {
    case LM75B_REG_TEMP:  return d->temp_raw;
    case LM75B_REG_CONF:  return d->conf;
    case LM75B_REG_THYST: return d->thyst;
    case LM75B_REG_TOS:   return d->tos;
    default: return 0x0000;
    }
}

/* temp() = (read16 >> 5, sign-extended) * 0.125 °C.
 * raw11 = milli°C / 125; store left-justified in bits 15..5. */
void lm75b_set_temp_mc(lm75b_t *d, int32_t milli_celsius)
{
    int32_t raw11 = div_round(milli_celsius, 125);
    if (raw11 > 1023) raw11 = 1023;      /* +127.875 °C ceiling             */
    if (raw11 < -1024) raw11 = -1024;    /* -128 °C floor                   */
    d->temp_raw = (uint16_t)((raw11 << 5) & 0xFFE0);
}

/* ======================================================================== */
/*  RX8130 (RTC stub)                                                       */
/* ======================================================================== */
void rx8130_reset(rx8130_t *d)
{
    for (int i = 0; i < 0x40; i++) d->reg[i] = 0;
    d->reg[RX8130_REG_SEC]   = 0x00; /* BCD 2026-09-08 12:30:00, Mon */
    d->reg[RX8130_REG_MIN]   = 0x30;
    d->reg[RX8130_REG_HOUR]  = 0x12;
    d->reg[RX8130_REG_WEEK]  = 0x01;
    d->reg[RX8130_REG_DAY]   = 0x08;
    d->reg[RX8130_REG_MONTH] = 0x09;
    d->reg[RX8130_REG_YEAR]  = 0x26;
    d->ptr = 0;
}
void rx8130_write_reg(rx8130_t *d, uint8_t reg, uint8_t val) { d->reg[reg & 0x3F] = val; }
uint8_t rx8130_read_reg(rx8130_t *d, uint8_t reg)            { return d->reg[reg & 0x3F]; }
