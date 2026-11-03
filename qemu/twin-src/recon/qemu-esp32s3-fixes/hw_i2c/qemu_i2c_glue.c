/* qemu_i2c_glue.c — QEMU I2CSlave wrappers for the TBD StamPLC twin.
 * ---------------------------------------------------------------------------
 * Binds the pure register cores (tbd_i2c.c) to QEMU's I²C bus and exposes the
 * physics values as live QOM properties the twin bridge drives over QMP:
 *
 *   qom-set  ina226 bus_mv=24000      current_ma=500     (physics → firmware)
 *   qom-set  lm75b  temp_mc=25000
 *   qom-set  aw9523 di=0x35           (drive digital inputs)
 *   qom-get  aw9523 relays            (read the firmware's relay outputs)
 *
 * Compiles inside the QEMU tree (add to hw/i2c and meson.build). NOT built by
 * the container test — the cores it wraps are tested by test/test_i2c_models.c.
 *
 * Attach on the esp32s3 internal I²C bus in the machine init (or via -device):
 *   i2c_slave_create_simple(bus, TYPE_TBD_AW9523, 0x59);
 *   i2c_slave_create_simple(bus, TYPE_TBD_INA226, 0x40);
 *   i2c_slave_create_simple(bus, TYPE_TBD_LM75B,  0x48);
 *   i2c_slave_create_simple(bus, TYPE_TBD_RX8130, 0x32);
 * See devices/README.md for the RB1.2 verification + the GDB-memory fallback.
 * ---------------------------------------------------------------------------
 */
#include "qemu/osdep.h"
#include "hw/i2c/tbd_bridge.h"
#include "qemu/bswap.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qom/object.h"

#include "tbd_i2c.h"
#include "qemu/timer.h"
#include "sysemu/runstate.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"

/* ======================================================================== */
/*  AW9523B  (8-bit registers, pointer + one data byte, no auto-increment)  */
/* ======================================================================== */
#define TYPE_TBD_AW9523 "tbd-aw9523"
OBJECT_DECLARE_SIMPLE_TYPE(TbdAw9523, TBD_AW9523)

struct TbdAw9523 {
    I2CSlave parent_obj;
    aw9523_t core;
    bool address_nack, output_write_nack;
    uint64_t injected_nacks;
    uint8_t  ptr;      /* selected register */
    int      phase;    /* 0 = expect pointer, 1 = expect data */
};

static int aw9523_i2c_event(I2CSlave *s, enum i2c_event event)
{
    TbdAw9523 *d = TBD_AW9523(s);
    if (d->address_nack && (event == I2C_START_SEND || event == I2C_START_RECV)) {
        ++d->injected_nacks;return 1;
    }
    if (event == I2C_START_SEND) d->phase = 0;   /* next byte is the pointer */
    return 0;
}

static uint8_t aw9523_i2c_recv(I2CSlave *s)
{
    TbdAw9523 *d = TBD_AW9523(s);
    return aw9523_read_reg(&d->core, d->ptr);    /* single-byte GPIO reads   */
}

static int aw9523_i2c_send(I2CSlave *s, uint8_t data)
{
    TbdAw9523 *d = TBD_AW9523(s);
    if (d->phase == 0) { d->ptr = data; d->phase = 1; }
    else {
        if (d->output_write_nack && d->ptr==0x02) {++d->injected_nacks;return 1;}
        aw9523_write_reg(&d->core, d->ptr, data);
    }
    return 0;
}

/* QOM: di (set physics inputs), relays (get firmware outputs) */
static void aw9523_get_relays_prop(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp) {
    TbdAw9523 *d = TBD_AW9523(obj);
    uint8_t val = aw9523_get_relays(&d->core);
    visit_type_uint8(v, name, &val, errp);
}
static void aw9523_set_di_prop(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp) {
    TbdAw9523 *d = TBD_AW9523(obj);
    uint8_t val;
    if (!visit_type_uint8(v, name, &val, errp)) return;
    aw9523_set_di(&d->core, val);
}
static void aw9523_get_di_prop(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp) {
    TbdAw9523 *d = TBD_AW9523(obj);
    uint8_t val = d->core.ext_di;
    visit_type_uint8(v, name, &val, errp);
}

/* Test controls belong to QMP, not guest-accessible device registers. */
static void aw9523_get_nack(Object *o, Visitor *v, const char *n, void *op, Error **e) {
    TbdAw9523 *d=TBD_AW9523(o);
    bool value=op ? d->output_write_nack : d->address_nack;
    visit_type_bool(v,n,&value,e);
}
static void aw9523_set_nack(Object *o, Visitor *v, const char *n, void *op, Error **e) {
    TbdAw9523 *d=TBD_AW9523(o);bool value;
    if(!visit_type_bool(v,n,&value,e))return;
    if(op)d->output_write_nack=value;else d->address_nack=value;
}
static void aw9523_get_nack_count(Object *o, Visitor *v, const char *n, void *op, Error **e) {
    uint64_t value=TBD_AW9523(o)->injected_nacks;visit_type_uint64(v,n,&value,e);
}
static void aw9523_realize(DeviceState *dev, Error **errp) {
    TbdAw9523 *d = TBD_AW9523(dev);
    aw9523_reset(&d->core);
    d->ptr = 0; d->phase = 0;
}
static void aw9523_init(Object *obj) {
    object_property_add(obj,"address-nack","bool",aw9523_get_nack,aw9523_set_nack,NULL,NULL);
    object_property_add(obj,"output-write-nack","bool",aw9523_get_nack,aw9523_set_nack,NULL,(void *)1);
    object_property_add(obj,"injected-nacks","uint64",aw9523_get_nack_count,NULL,NULL,NULL);
    object_property_add(obj, "relays", "uint8", aw9523_get_relays_prop, NULL, NULL, NULL);
    object_property_add(obj, "di", "uint8", aw9523_get_di_prop, aw9523_set_di_prop, NULL, NULL);
}

static const VMStateDescription vmstate_aw9523 = {
    .name = TYPE_TBD_AW9523, .version_id = 2, .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, TbdAw9523),
        VMSTATE_UINT8(core.out0, TbdAw9523),
        VMSTATE_UINT8(core.ext_di, TbdAw9523),
        VMSTATE_UINT8(ptr, TbdAw9523),
        VMSTATE_BOOL_V(address_nack, TbdAw9523, 2),
        VMSTATE_BOOL_V(output_write_nack, TbdAw9523, 2),
        VMSTATE_UINT64_V(injected_nacks, TbdAw9523, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void aw9523_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    k->event = aw9523_i2c_event;
    k->recv  = aw9523_i2c_recv;
    k->send  = aw9523_i2c_send;
    dc->realize = aw9523_realize;
    dc->vmsd = &vmstate_aw9523;
}

/* ======================================================================== */
/*  Shared 16-bit pointer-register machine for INA226 + LM75B               */
/* ======================================================================== */
/* Both are: START_SEND → [ptr] ([msb][lsb])? ; repeated START_RECV → msb,lsb */

/* ---- INA226 ---- */
#define TYPE_TBD_INA226 "tbd-ina226"
OBJECT_DECLARE_SIMPLE_TYPE(TbdIna226, TBD_INA226)
struct TbdIna226 {
    I2CSlave parent_obj; ina226_t core;
    uint8_t ptr; int wphase; int rbyte; uint8_t wmsb;
};
static int ina226_i2c_event(I2CSlave *s, enum i2c_event e) {
    TbdIna226 *d = TBD_INA226(s);
    if (e == I2C_START_SEND) d->wphase = 0;
    else if (e == I2C_START_RECV) d->rbyte = 0;
    return 0;
}
static uint8_t ina226_i2c_recv(I2CSlave *s) {
    TbdIna226 *d = TBD_INA226(s);
    uint16_t v = ina226_read_reg(&d->core, d->ptr);
    uint8_t b = (d->rbyte == 0) ? (v >> 8) : (v & 0xFF);
    if (d->rbyte++ == 1) { d->ptr++; d->rbyte = 0; }  /* auto-advance */
    return b;
}
static int ina226_i2c_send(I2CSlave *s, uint8_t data) {
    TbdIna226 *d = TBD_INA226(s);
    if (d->wphase == 0)      { d->ptr = data; d->wphase = 1; }
    else if (d->wphase == 1) { d->wmsb = data; d->wphase = 2; }
    else { ina226_write_reg(&d->core, d->ptr, (d->wmsb << 8) | data); d->wphase = 1; }
    return 0;
}
static void ina226_set_busmv(Object *o, Visitor *v, const char *n, void *op, Error **errp){
    TbdIna226 *d = TBD_INA226(o); int32_t x;
    if (!visit_type_int32(v, n, &x, errp)) return;
    ina226_set_bus_mv(&d->core, x);
}
static void ina226_set_curma(Object *o, Visitor *v, const char *n, void *op, Error **errp){
    TbdIna226 *d = TBD_INA226(o); int32_t x;
    if (!visit_type_int32(v, n, &x, errp)) return;
    ina226_set_current_ma(&d->core, x);
}
static void ina226_realize(DeviceState *dev, Error **errp){ ina226_reset(&TBD_INA226(dev)->core); }
static void ina226_init(Object *o){
    object_property_add(o, "bus_mv", "int", NULL, ina226_set_busmv, NULL, NULL);
    object_property_add(o, "current_ma", "int", NULL, ina226_set_curma, NULL, NULL);
}
static void ina226_class_init(ObjectClass *klass, void *data){
    DeviceClass *dc = DEVICE_CLASS(klass); I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    k->event = ina226_i2c_event; k->recv = ina226_i2c_recv; k->send = ina226_i2c_send;
    dc->realize = ina226_realize;
}

/* ---- LM75B ---- */
#define TYPE_TBD_LM75B "tbd-lm75b"
OBJECT_DECLARE_SIMPLE_TYPE(TbdLm75b, TBD_LM75B)
struct TbdLm75b {
    I2CSlave parent_obj; lm75b_t core;
    uint8_t ptr; int wphase; int rbyte; uint8_t wmsb;
};
static int lm75b_i2c_event(I2CSlave *s, enum i2c_event e){
    TbdLm75b *d = TBD_LM75B(s);
    if (e == I2C_START_SEND) d->wphase = 0; else if (e == I2C_START_RECV) d->rbyte = 0;
    return 0;
}
static uint8_t lm75b_i2c_recv(I2CSlave *s){
    TbdLm75b *d = TBD_LM75B(s);
    uint16_t v = lm75b_read_reg(&d->core, d->ptr);
    /* CONF is 8-bit; TEMP/THYST/TOS are 16-bit big-endian */
    if (d->ptr == LM75B_REG_CONF) return (uint8_t)v;
    uint8_t b = (d->rbyte == 0) ? (v >> 8) : (v & 0xFF);
    d->rbyte ^= 1;
    return b;
}
static int lm75b_i2c_send(I2CSlave *s, uint8_t data){
    TbdLm75b *d = TBD_LM75B(s);
    if (d->wphase == 0) { d->ptr = data; d->wphase = 1; }
    else if (d->ptr == LM75B_REG_CONF) { lm75b_write_reg(&d->core, d->ptr, data); }
    else if (d->wphase == 1) { d->wmsb = data; d->wphase = 2; }
    else { lm75b_write_reg(&d->core, d->ptr, (d->wmsb << 8) | data); d->wphase = 1; }
    return 0;
}
static void lm75b_set_tempmc(Object *o, Visitor *v, const char *n, void *op, Error **errp){
    TbdLm75b *d = TBD_LM75B(o); int32_t x;
    if (!visit_type_int32(v, n, &x, errp)) return;
    lm75b_set_temp_mc(&d->core, x);
}
static void lm75b_realize(DeviceState *dev, Error **errp){ lm75b_reset(&TBD_LM75B(dev)->core); }
static void lm75b_init(Object *o){
    object_property_add(o, "temp_mc", "int", NULL, lm75b_set_tempmc, NULL, NULL);
}
static void lm75b_class_init(ObjectClass *klass, void *data){
    DeviceClass *dc = DEVICE_CLASS(klass); I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    k->event = lm75b_i2c_event; k->recv = lm75b_i2c_recv; k->send = lm75b_i2c_send;
    dc->realize = lm75b_realize;
}

/* ======================================================================== */
/*  RX8130 (RTC stub — pointer + auto-increment, single bytes)              */
/* ======================================================================== */
#define TYPE_TBD_RX8130 "tbd-rx8130"
OBJECT_DECLARE_SIMPLE_TYPE(TbdRx8130, TBD_RX8130)
struct TbdRx8130 { I2CSlave parent_obj; rx8130_t core; int wphase; };
static int rx8130_i2c_event(I2CSlave *s, enum i2c_event e){
    if (e == I2C_START_SEND) TBD_RX8130(s)->wphase = 0;
    return 0;
}
static uint8_t rx8130_i2c_recv(I2CSlave *s){
    TbdRx8130 *d = TBD_RX8130(s);
    uint8_t b = rx8130_read_reg(&d->core, d->core.ptr); d->core.ptr++; return b;
}
static int rx8130_i2c_send(I2CSlave *s, uint8_t data){
    TbdRx8130 *d = TBD_RX8130(s);
    if (d->wphase == 0) { d->core.ptr = data; d->wphase = 1; }
    else { rx8130_write_reg(&d->core, d->core.ptr, data); d->core.ptr++; }
    return 0;
}
static void rx8130_realize(DeviceState *dev, Error **errp){ rx8130_reset(&TBD_RX8130(dev)->core); }
static void rx8130_class_init(ObjectClass *klass, void *data){
    DeviceClass *dc = DEVICE_CLASS(klass); I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    k->event = rx8130_i2c_event; k->recv = rx8130_i2c_recv; k->send = rx8130_i2c_send;
    dc->realize = rx8130_realize;
}

/* ======================================================================== */
/*  PI4IOE5V6408 (IO Expander A @ 0x43: 3 front buttons, 3 status-LED pins,  */
/*  LCD backlight on P7). 8-bit register file, pointer + single data byte.   */
/*  Modeling it is load-bearing: M5Unified probes it in begin() (reads reg   */
/*  0x01, requires non-zero) and the M5StamPLC bring-up + display_ui button  */
/*  poll touch it every loop. With no slave here, every access NACKs, the    */
/*  LovyanGFX i2c context latches connection_lost, and the driver spins in   */
/*  its bit-banged force-stop recovery forever (TBD "Wall 5").             */
/* ======================================================================== */
#define TYPE_TBD_PI4IOE "tbd-pi4ioe"
OBJECT_DECLARE_SIMPLE_TYPE(TbdPi4ioe, TBD_PI4IOE)

struct TbdPi4ioe {
    I2CSlave parent_obj;
    uint8_t  regs[128];
    uint8_t  ptr;      /* selected register */
    int      phase;    /* 0 = expect pointer, 1 = expect data */
};

static void pi4ioe_reset_regs(TbdPi4ioe *d)
{
    memset(d->regs, 0, sizeof(d->regs));
    d->regs[0x01] = 0xA0;   /* Device ID & Control: non-zero so begin() detects the chip */
    d->regs[0x0F] = 0xFF;   /* Input Status: inputs idle-high (front buttons unpressed, active-low) */
    d->ptr = 0; d->phase = 0;
}

static int pi4ioe_i2c_event(I2CSlave *s, enum i2c_event event)
{
    TbdPi4ioe *d = TBD_PI4IOE(s);
    if (event == I2C_START_SEND) d->phase = 0;   /* next byte selects the register */
    return 0;
}
static uint8_t pi4ioe_i2c_recv(I2CSlave *s)
{
    TbdPi4ioe *d = TBD_PI4IOE(s);
    return d->regs[d->ptr & 0x7F];               /* single-byte register reads */
}
static int pi4ioe_i2c_send(I2CSlave *s, uint8_t data)
{
    TbdPi4ioe *d = TBD_PI4IOE(s);
    if (d->phase == 0) { d->ptr = data & 0x7F; d->phase = 1; }
    else               { d->regs[d->ptr & 0x7F] = data; }
    return 0;
}

/* QOM: input (drive reg 0x0F = buttons/inputs), output (read reg 0x05 = LEDs/backlight) */
static void pi4ioe_set_input_prop(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp) {
    TbdPi4ioe *d = TBD_PI4IOE(obj);
    uint8_t val;
    if (!visit_type_uint8(v, name, &val, errp)) return;
    d->regs[0x0F] = val;
}
static void pi4ioe_get_input_prop(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp) {
    TbdPi4ioe *d = TBD_PI4IOE(obj);
    uint8_t val = d->regs[0x0F];
    visit_type_uint8(v, name, &val, errp);
}
static void pi4ioe_get_output_prop(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp) {
    TbdPi4ioe *d = TBD_PI4IOE(obj);
    uint8_t val = d->regs[0x05];
    visit_type_uint8(v, name, &val, errp);
}

static void pi4ioe_realize(DeviceState *dev, Error **errp) {
    pi4ioe_reset_regs(TBD_PI4IOE(dev));
}
static void pi4ioe_instance_init(Object *obj) {
    object_property_add(obj, "input", "uint8",
                        pi4ioe_get_input_prop, pi4ioe_set_input_prop, NULL, NULL);
    object_property_add(obj, "output", "uint8",
                        pi4ioe_get_output_prop, NULL, NULL, NULL);
}
static const VMStateDescription vmstate_pi4ioe = {
    .name = TYPE_TBD_PI4IOE, .version_id = 1, .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, TbdPi4ioe),
        VMSTATE_UINT8_ARRAY(regs, TbdPi4ioe, 128),
        VMSTATE_UINT8(ptr, TbdPi4ioe),
        VMSTATE_END_OF_LIST()
    }
};
static void pi4ioe_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    k->event = pi4ioe_i2c_event;
    k->recv  = pi4ioe_i2c_recv;
    k->send  = pi4ioe_i2c_send;
    dc->realize = pi4ioe_realize;
    dc->vmsd = &vmstate_pi4ioe;
}

/* ======================================================================== */
/*  StamPLC IO expansion module (0x20-0x2F)  — the isolated 2-channel        */
/*  analog-in / relay expansion pack. Its own MCU exposes an 8-bit register  */
/*  map (pointer + auto-increment) with per-channel INA226s:                 */
/*    0x00 V_CH1(i16 mV) 0x02 I_CH1(i32 uA) 0x06 V_CH2 0x08 I_CH2            */
/*    0x10 IO_CONTROL  (b0 SSR1, b1 SSR2, b2/3 CH pull-ups, b4 mech relay)   */
/*    0xFB SYSTEM_STATUS (0=ready) 0xFE FIRMWARE_VER 0xFF ADDR_CONFIG        */
/*  Physical: 2× 0-10V / 0-20mA field inputs, 2× SSR (60V 2.5A), 1 mech      */
/*  relay (SRD-5VDC), CA-IS3020S-isolated. Modeled so ioexp_begin() detects  */
/*  it and CH1/CH2 (Modbus regs 10-13) carry live process values.            */
/* ======================================================================== */
#define TYPE_TBD_IOEXP "tbd-ioexp"
OBJECT_DECLARE_SIMPLE_TYPE(TbdIoexp, TBD_IOEXP)
struct TbdIoexp {
    I2CSlave parent_obj;
    uint8_t firmware_revision;
    bool experimental_deadlines, deadline_armed, deadline_fired;
    QEMUTimer *deadline_timer;
    Error *deadline_migration_blocker;
    VMChangeStateEntry *deadline_state_handler;
    uint64_t deadline_epoch, deadline_serial;
    int64_t deadline_ns, deadline_observed_ns;
    int32_t deadline_stop_result;
    bool deadline_callback_vcpu;
    int64_t deadline_after_stop_ns;
    uint8_t  regs[256];
    int64_t ready_ms;
    bool converting;
    uint8_t  ptr;
    int      phase;    /* 0 = expect pointer, 1 = data (auto-increment)      */
};
static TbdIoexp *deadline_owner; /* One administrative whole-VM stop authority. */
static void ioexp_reset_regs(TbdIoexp *d)
{
    memset(d->regs, 0, sizeof(d->regs));
    d->regs[0xFB] = 0x00;   /* SYSTEM_STATUS: 0 = ready (both INA226s init'd) */
    d->regs[0xFE] = d->firmware_revision; /* Explicit bench identity; default preserves historical profile. */
    d->regs[0xFF] = I2C_SLAVE(d)->address;   /* ADDR_CONFIG: default I2C address               */
    d->ptr = 0; d->phase = 0;
}
static int ioexp_i2c_event(I2CSlave *s, enum i2c_event event)
{
    TbdIoexp *d = TBD_IOEXP(s);
    if (event == I2C_START_SEND) d->phase = 0;   /* next byte selects register */
    return 0;
}
static uint8_t ioexp_i2c_recv(I2CSlave *s)       /* pointer + auto-increment   */
{
    TbdIoexp *d = TBD_IOEXP(s);
    uint8_t v = d->regs[d->ptr];
    if(d->ptr==0x2d)d->regs[0x1d]&=~0x80;
    d->ptr++;
    return v;
}
static int ioexp_i2c_send(I2CSlave *s, uint8_t data)
{
    TbdIoexp *d = TBD_IOEXP(s);
    if (d->phase == 0) { d->ptr = data; d->phase = 1; }
    else               { d->regs[d->ptr] = data; d->ptr++; }   /* writable regs */
    return 0;
}
/* physics → the two field channels (V in mV int16, I in uA int32, both LE). */
static void ioexp_phys_set(TbdIoexp *d, int v1_mv, int i1_ua, int v2_mv, int i2_ua)
{
    d->regs[0x00] = v1_mv & 0xFF; d->regs[0x01] = (v1_mv >> 8) & 0xFF;
    d->regs[0x02] = i1_ua & 0xFF; d->regs[0x03] = (i1_ua >> 8) & 0xFF;
    d->regs[0x04] = (i1_ua >> 16) & 0xFF; d->regs[0x05] = (i1_ua >> 24) & 0xFF;
    d->regs[0x06] = v2_mv & 0xFF; d->regs[0x07] = (v2_mv >> 8) & 0xFF;
    d->regs[0x08] = i2_ua & 0xFF; d->regs[0x09] = (i2_ua >> 8) & 0xFF;
    d->regs[0x0A] = (i2_ua >> 16) & 0xFF; d->regs[0x0B] = (i2_ua >> 24) & 0xFF;
}

/* One QMP transaction publishes all four physical input values. Validate into
 * locals first so a malformed/partial sample cannot change the register file.
 * External ownership requires the machine's internal skid writer disabled. */
static void ioexp_set_analog(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    int32_t v1, i1, v2, i2;
    bool ok;
    if (!getenv("TBD_NOPHYS")) {
        error_setg(errp, "analog-inputs requires TBD_NOPHYS (one physics writer)");
        return;
    }
    if (!visit_start_struct(v, name, NULL, 0, errp)) {
        return;
    }
    ok = visit_type_int32(v, "ch1_mv", &v1, errp) &&
         visit_type_int32(v, "ch1_ua", &i1, errp) &&
         visit_type_int32(v, "ch2_mv", &v2, errp) &&
         visit_type_int32(v, "ch2_ua", &i2, errp) &&
         visit_check_struct(v, errp);
    visit_end_struct(v, NULL);
    if (!ok) {
        return;
    }
    if (v1 < 0 || v1 > 10000 || v2 < 0 || v2 > 10000 ||
        i1 < 0 || i1 > 20000 || i2 < 0 || i2 > 20000) {
        error_setg(errp, "analog-inputs requires 0..10000 mV and 0..20000 uA");
        return;
    }
    ioexp_phys_set(TBD_IOEXP(obj), v1, i1, v2, i2);
}

static void ioexp_get_analog(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    TbdIoexp *d = TBD_IOEXP(obj);
    int32_t v1 = d->regs[0] | (d->regs[1] << 8);
    int32_t i1 = ldl_le_p(&d->regs[2]);
    int32_t v2 = d->regs[6] | (d->regs[7] << 8);
    int32_t i2 = ldl_le_p(&d->regs[8]);
    if (!visit_start_struct(v, name, NULL, 0, errp)) {
        return;
    }
    visit_type_int32(v, "ch1_mv", &v1, errp);
    visit_type_int32(v, "ch1_ua", &i1, errp);
    visit_type_int32(v, "ch2_mv", &v2, errp);
    visit_type_int32(v, "ch2_ua", &i2, errp);
    visit_end_struct(v, NULL);
}

/* Administrative observation only: reading this property never executes guest
 * instructions or grants time. QMP serialization is not a causal-time grant.
 * QEMU virtual nanoseconds are neither host time nor calibrated MCU cycles. */
static void ioexp_get_virtual_time(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    visit_type_int64(v, name, &now, errp);
}

/* Experimental virtual-time stop only. A fired timer is NOT a completed
 * firmware scan or proof that all other events at this time have drained.
 * Clients must confirm paused status, epoch/serial, and exact clock equality.
 * No migration/snapshot or physical wall-time guarantees are provided. */
static void ioexp_deadline_fire(void *opaque)
{
    TbdIoexp *d = opaque;
    d->deadline_armed = false;
    d->deadline_fired = true;
    d->deadline_observed_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    icount_set_deadline_fence(true);
    d->deadline_callback_vcpu = qemu_in_vcpu_thread();
    d->deadline_stop_result = vm_stop(RUN_STATE_PAUSED);
    d->deadline_after_stop_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}
static void ioexp_set_deadline(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    TbdIoexp *d = TBD_IOEXP(obj);
    int64_t deadline;
    if (!visit_type_int64(v, name, &deadline, errp)) return;
    if (!d->deadline_timer || icount_enabled() != ICOUNT_PRECISE ||
        !getenv("TBD_NOPHYS") || !runstate_check(RUN_STATE_PAUSED)) {
        error_setg(errp, "deadline requires experimental-deadlines, fixed icount, TBD_NOPHYS and paused VM");
        return;
    }
    if (d->deadline_armed || deadline <= qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) ||
        d->deadline_serial == UINT64_MAX) {
        error_setg(errp, "deadline must be future, non-overlapping and have an available serial");
        return;
    }
    ++d->deadline_serial;
    d->deadline_ns = deadline;
    d->deadline_observed_ns = -1;
    d->deadline_stop_result = -1;
    d->deadline_after_stop_ns = -1;
    d->deadline_callback_vcpu = false;
    d->deadline_fired = false;
    d->deadline_armed = true;
    timer_mod_ns(d->deadline_timer, deadline);
}
static void ioexp_get_deadline(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    TbdIoexp *d = TBD_IOEXP(obj);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    bool running = runstate_is_running();
    bool paused = runstate_check(RUN_STATE_PAUSED);
    if (!visit_start_struct(v, name, NULL, 0, errp)) return;
    visit_type_uint64(v, "epoch", &d->deadline_epoch, errp);
    visit_type_uint64(v, "serial", &d->deadline_serial, errp);
    visit_type_bool(v, "armed", &d->deadline_armed, errp);
    visit_type_bool(v, "fired", &d->deadline_fired, errp);
    visit_type_bool(v, "running", &running, errp);
    visit_type_bool(v, "paused", &paused, errp);
    visit_type_int64(v, "requested_ns", &d->deadline_ns, errp);
    visit_type_int64(v, "observed_ns", &d->deadline_observed_ns, errp);
    visit_type_int64(v, "current_ns", &now, errp);
    visit_type_int32(v, "stop_result", &d->deadline_stop_result, errp);
    visit_type_bool(v, "callback_vcpu", &d->deadline_callback_vcpu, errp);
    visit_type_int64(v, "after_stop_call_ns", &d->deadline_after_stop_ns, errp);
    visit_end_struct(v, NULL);
}
static void ioexp_deadline_vm_state(void *opaque, bool running, RunState state)
{
    TbdIoexp *d = opaque;
    if (!running && icount_deadline_fenced()) {
        /* do_vm_stop notifies only after pausing all guest CPUs. */
        if (state != RUN_STATE_PAUSED) d->deadline_fired = false;
        icount_set_deadline_fence(false);
    }
}
static void ioexp_deadline_reset(DeviceState *dev)
{
    TbdIoexp *d = TBD_IOEXP(dev);
    if (!d->deadline_timer) return;
    icount_set_deadline_fence(false);
    timer_del(d->deadline_timer);
    ++d->deadline_epoch;
    d->deadline_armed = d->deadline_fired = false;
    d->deadline_ns = d->deadline_observed_ns = -1;
    d->deadline_stop_result = -1;
    d->deadline_after_stop_ns = -1;
    d->deadline_callback_vcpu = false;
    /* Serial remains monotonic across resets; no peripheral register change. */
}
static void ioexp_unrealize(DeviceState *dev)
{
    TbdIoexp *d = TBD_IOEXP(dev);
    if (d->deadline_state_handler) {
        qemu_del_vm_change_state_handler(d->deadline_state_handler);
        d->deadline_state_handler = NULL;
    }
    if (deadline_owner == d) icount_set_deadline_fence(false);
    if (d->deadline_timer) {
        timer_free(d->deadline_timer);
        d->deadline_timer = NULL;
    }
    if (d->deadline_migration_blocker) migrate_del_blocker(&d->deadline_migration_blocker);
    if (deadline_owner == d) deadline_owner = NULL;
}

static void ioexp_init(Object *obj)
{
    object_property_add(obj, "deadline-ns", "int64", NULL,
                        ioexp_set_deadline, NULL, NULL);
    object_property_add(obj, "deadline-state", "tbd-deadline-state",
                        ioexp_get_deadline, NULL, NULL, NULL);
    object_property_add(obj, "virtual-time-ns", "int64",
                        ioexp_get_virtual_time, NULL, NULL, NULL);
    object_property_add(obj, "analog-inputs", "tbd-analog-inputs",
                        ioexp_get_analog, ioexp_set_analog, NULL, NULL);
}
static Property ioexp_props[] = {
    DEFINE_PROP_BOOL("experimental-deadlines", TbdIoexp, experimental_deadlines, false),
    DEFINE_PROP_UINT8("firmware-revision", TbdIoexp, firmware_revision, 0x11),
    DEFINE_PROP_END_OF_LIST()
};
static void ioexp_realize(DeviceState *dev, Error **errp) {
    TbdIoexp *d = TBD_IOEXP(dev);
    if (d->firmware_revision == 0xff) {
        error_setg(errp, "IO expansion revision 0xff denotes unavailable identity");
        return;
    }
    ioexp_reset_regs(d);
    if (d->experimental_deadlines) {
        if (deadline_owner) {
            error_setg(errp, "Only one experimental deadline owner is permitted");
            return;
        }
        error_setg(&d->deadline_migration_blocker, "Experimental deadlines do not support migration or snapshot save");
        if (migrate_add_blocker(&d->deadline_migration_blocker, errp) < 0) return;
        deadline_owner = d;
        d->deadline_state_handler = qemu_add_vm_change_state_handler(ioexp_deadline_vm_state, d);
        d->deadline_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ioexp_deadline_fire, d);
    }
}
static void ioexp_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass); I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    k->event = ioexp_i2c_event; k->recv = ioexp_i2c_recv; k->send = ioexp_i2c_send;
    dc->realize = ioexp_realize;
    dc->unrealize = ioexp_unrealize;
    device_class_set_legacy_reset(dc, ioexp_deadline_reset);
    device_class_set_props(dc, ioexp_props);
}

/* ------------------------------------------------------------------------ */
/*  Grove ENV: Bosch BME680 (bme68x driver) on the external Grove RED port    */
/*  = I2C1 @0x77.  A pointer+auto-increment register file seeded with a       */
/*  calibration set + field-data block solved off-line (bme_solve.py) so the  */
/*  firmware's own FPU compensation yields a fixed, sane environment:         */
/*      24.0 C  /  45 % RH  /  1013.0 hPa   (env_ok=1).                        */
/*  Forced-mode timing, new-data/index and gas flags are modeled. Chemical    */
/*  and thermal transients are not calibrated by this protocol fixture.       */
#define TYPE_TBD_BME680 "tbd-bme680"
OBJECT_DECLARE_SIMPLE_TYPE(TbdBme680, TBD_BME680)
struct TbdBme680 {
    I2CSlave parent_obj;
    uint8_t  regs[256];
    int64_t ready_ms;
    bool converting;
    uint8_t variant; // Bosch gas-low(0) or gas-high(1) register layout.
    bool config_write_nack;
    uint64_t config_write_nacks;
    uint8_t  ptr;
    int      phase;    /* 0 = expect pointer, 1 = data (auto-increment) */
};
static void bme680_reset_regs(TbdBme680 *d)
{
    static const struct { uint8_t r, v; } seed[] = {
        /* field-data block @0x1D (BME68X_REG_FIELD0): status + press/temp/hum ADC */
        {0x1D,0x80},{0x1F,0x4E},{0x20,0xCB},{0x21,0x00},{0x22,0x78},{0x23,0x63},
        {0x24,0xD0},{0x25,0x50},{0x26,0x6C},
        /* COEFF1 @0x8A..0xA0 (23 bytes) — par_t/par_p/par_h coefficients */
        {0x8A,0x2A},{0x8B,0x67},{0x8C,0x03},{0x8D,0x00},{0x8E,0x5C},{0x8F,0x90},
        {0x90,0x22},{0x91,0xD7},{0x92,0x58},{0x93,0x00},{0x94,0xDC},{0x95,0x1D},
        {0x96,0x6E},{0x97,0xFF},{0x98,0x0F},{0x99,0x1E},{0x9A,0x00},{0x9B,0x00},
        {0x9C,0x77},{0x9D,0xF4},{0x9E,0x69},{0x9F,0xF2},{0xA0,0x1E},
        /* chip id (BME68X_REG_CHIP_ID) */
        {0xD0,0x61},
        /* COEFF2 @0xE1..0xEE (14 bytes) */
        {0xE1,0x3F},{0xE2,0x92},{0xE3,0x2E},{0xE4,0x00},{0xE5,0x2D},{0xE6,0x14},
        {0xE7,0x78},{0xE8,0x9C},{0xE9,0xC8},{0xEA,0x65},{0xEB,0x00},{0xEC,0x00},
        {0xED,0x00},{0xEE,0x00},
    };
    memset(d->regs, 0, sizeof(d->regs));
    for (unsigned i = 0; i < ARRAY_SIZE(seed); i++)
        d->regs[seed[i].r] = seed[i].v;
    d->regs[0xf0]=d->variant;
    d->ptr = 0; d->phase = 0;d->converting=false;d->ready_ms=0;d->regs[0x1d]=0;
}
static void bme680_update(TbdBme680 *d)
{
    if(d->converting && qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL)>=d->ready_ms) {
        if(getenv("TBD_PAHUB") && !strcmp(getenv("TBD_PAHUB"),"env-stale"))return;
        d->converting=false;d->regs[0x74]&=~3;d->regs[0x1d]=0x80;
        if(!getenv("TBD_PAHUB") || strcmp(getenv("TBD_PAHUB"),"env-static-index"))++d->regs[0x1e];
        /* Bosch driver selects field bytes13/14 for gas-low and15/16
         * for gas-high. Keep the unused channel clear, not falsely valid. */
        unsigned gas=d->variant ? 0x2c : 0x2a;
        d->regs[gas]=0x40;d->regs[gas+1]=0x35; /* synthetic ADC/range + valid/stable */
    }
}
static int bme680_i2c_event(I2CSlave *s, enum i2c_event event)
{
    TbdBme680 *d = TBD_BME680(s);
    bme680_update(d);
    if (event == I2C_START_SEND) d->phase = 0;   /* next byte selects register */
    return 0;
}
static uint8_t bme680_i2c_recv(I2CSlave *s)      /* pointer + auto-increment */
{
    TbdBme680 *d = TBD_BME680(s);
    uint8_t v = d->regs[d->ptr];
    if(d->ptr==0x2d)d->regs[0x1d]&=~0x80;
    d->ptr++;
    return v;
}
static int bme680_i2c_send(I2CSlave *s, uint8_t data)
{
    TbdBme680 *d = TBD_BME680(s);
    /* Bosch API sends register/value pairs in a single I2C write. Reads
     * auto-increment, writes take a new register after each data byte. */
    if (d->phase == 0) { d->ptr = data; d->phase = 1; }
    else {
        uint8_t reg=d->ptr;d->phase=0;
        /* Injected write-ACK failure: reject the CTRL_MEAS data byte
         * before changing the register. Address/pointer/identity reads still
         * work. The controller must propagate this actual i2c_send failure.
         */
        if (reg == 0x74 && d->config_write_nack) {
            d->config_write_nacks++;
            return -1;
        }
        if(reg==0xe0 && data==0xb6) { bme680_reset_regs(d);return 0; }
        d->regs[reg]=data;
        if(reg==0x74 && (data&3)==1) {
            d->converting=true;d->ready_ms=qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL)+190;
            d->regs[0x1d]=0;
        }
    }
    return 0;
}
static void bme680_realize(DeviceState *dev, Error **errp) {
    TbdBme680 *d=TBD_BME680(dev);
    if(d->variant>1){error_setg(errp,"Unsupported BME68x variant");return;}
    bme680_reset_regs(d);
}
static Property bme680_props[]={
    DEFINE_PROP_UINT8("variant",TbdBme680,variant,0),
    DEFINE_PROP_END_OF_LIST()
};
static void bme680_get_config_nack(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    bool value = TBD_BME680(obj)->config_write_nack;
    visit_type_bool(v, name, &value, errp);
}
static void bme680_set_config_nack(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    bool value;
    if (visit_type_bool(v, name, &value, errp)) {
        TBD_BME680(obj)->config_write_nack = value;
    }
}
static void bme680_get_config_nacks(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    uint64_t value = TBD_BME680(obj)->config_write_nacks;
    visit_type_uint64(v, name, &value, errp);
}
static void bme680_instance_init(Object *obj)
{
    object_property_add(obj, "config-write-nack", "bool", bme680_get_config_nack,
                        bme680_set_config_nack, NULL, NULL);
    object_property_add(obj, "config-write-nacks", "uint64", bme680_get_config_nacks,
                        NULL, NULL, NULL);
}
static void bme680_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass); I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    k->event = bme680_i2c_event; k->recv = bme680_i2c_recv; k->send = bme680_i2c_send;
    dc->realize = bme680_realize;
    device_class_set_props(dc,bme680_props);
}

/* ======================================================================== */
/*  Type registration                                                       */
/* ======================================================================== */
static const TypeInfo tbd_i2c_types[] = {
    { .name = TYPE_TBD_AW9523, .parent = TYPE_I2C_SLAVE,
      .instance_size = sizeof(TbdAw9523), .instance_init = aw9523_init,
      .class_init = aw9523_class_init },
    { .name = TYPE_TBD_INA226, .parent = TYPE_I2C_SLAVE,
      .instance_size = sizeof(TbdIna226), .instance_init = ina226_init,
      .class_init = ina226_class_init },
    { .name = TYPE_TBD_LM75B, .parent = TYPE_I2C_SLAVE,
      .instance_size = sizeof(TbdLm75b), .instance_init = lm75b_init,
      .class_init = lm75b_class_init },
    { .name = TYPE_TBD_RX8130, .parent = TYPE_I2C_SLAVE,
      .instance_size = sizeof(TbdRx8130), .class_init = rx8130_class_init },
    { .name = TYPE_TBD_PI4IOE, .parent = TYPE_I2C_SLAVE,
      .instance_size = sizeof(TbdPi4ioe), .instance_init = pi4ioe_instance_init,
      .class_init = pi4ioe_class_init },
    { .name = TYPE_TBD_IOEXP, .parent = TYPE_I2C_SLAVE,
      .instance_size = sizeof(TbdIoexp), .instance_init = ioexp_init,
      .class_init = ioexp_class_init },
    { .name = TYPE_TBD_BME680, .parent = TYPE_I2C_SLAVE,
      .instance_size = sizeof(TbdBme680), .instance_init = bme680_instance_init,
      .class_init = bme680_class_init },
};
DEFINE_TYPES(tbd_i2c_types)

/* ======================================================================== */
/*  TBD twin: internal process physics                                    */
/* ------------------------------------------------------------------------ */
/*  A small, self-contained process so the twin's sensors respond to the     */
/*  firmware's own relay outputs — the Modbus image then shows a live        */
/*  process, and an unauthenticated coil write (which reaches the relays via */
/*  the PLC image) produces a *visible* change instead of static zeros.      */
/*                                                                            */
/*  Model (a heater/pump skid, all values in the sensors the firmware reads):*/
/*    relay0 = PUMP  → motor current on INA226 (~+480 mA), supply sag        */
/*    relay1 = HEAT  → LM75B temperature rises toward 60 °C (1st-order lag)   */
/*    relay2 = AUX   → extra current draw                                     */
/*  Idle current ~40 mA, ambient 22 °C, 24 V bus sagging under load.         */
/*  Runs on a 100 ms virtual-clock timer. Disable with TBD_NOPHYS to let    */
/*  an external bridge drive the sensors via QMP qom-set instead.            */
static aw9523_t   *s_phys_aw;
static ina226_t   *s_phys_ina;
static lm75b_t    *s_phys_lm;
static TbdIoexp *s_phys_ioexp;               /* IO-expansion field channels  */
static QEMUTimer  *s_phys_timer;
static int32_t     s_phys_temp_mc  = 22000;    /* integrated skid temperature  */
static int32_t     s_phys_level_mv = 1000;     /* integrated tank level (CH1)   */

static void tbd_physics_tick(void *opaque)
{
    uint8_t relays = aw9523_get_relays(s_phys_aw);
    int motor = relays & 0x01;                       /* QX0 = pump motor      */
    int leds  = ((relays >> 1) & 1) + ((relays >> 2) & 1) + ((relays >> 3) & 1);

    /* Base-board power monitor (INA226 @0x40) + skid temperature (LM75B).     */
    int32_t cur_ma = 40 + (motor ? 480 : 0) + leds * 12;
    int32_t vin_mv = 24000 - (motor ? 400 : 0) - leds * 30;
    int32_t t_tgt  = motor ? 55000 : 22000;          /* motor heats the skid  */
    s_phys_temp_mc += (t_tgt - s_phys_temp_mc) / 16;  /* ~1.6 s time constant  */
    ina226_set_current_ma(s_phys_ina, cur_ma);
    ina226_set_bus_mv(s_phys_ina, vin_mv);
    lm75b_set_temp_mc(s_phys_lm, s_phys_temp_mc);

    /* IO-expansion field sensors: CH1 = tank level on a 0-10V transmitter that
     * the pump fills (integrates up while the motor runs, drains when idle);
     * CH2 = a 4-20mA flow loop that reads flow only while the pump runs. */
    if (s_phys_ioexp) {
        int32_t l_tgt = motor ? 8500 : 800;
        s_phys_level_mv += (l_tgt - s_phys_level_mv) / 24;   /* slow tank fill */
        int32_t ch2_flow_ua = motor ? 18000 : 4000;          /* 4-20mA loop    */
        ioexp_phys_set(s_phys_ioexp, s_phys_level_mv, 0, /*V/I CH1*/
                       0, ch2_flow_ua);                      /* V/I CH2        */
    }

    timer_mod(s_phys_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
}

/* TBD twin: raw count for the Grove HX711 load cell under the process tank.
 * The bit-bang HX711 model (hw/gpio/esp32_gpio.c) clocks this 24-bit value out.
 * Empty tare ~ +136k counts; rises as the pump fills the tank (level on CH1), so
 * an unauth coil write that starts the pump also shows up on the weight scale. */
int tbd_twin_weight_raw(void)
{
    int32_t lvl = s_phys_level_mv;         /* ~800 (empty) .. ~8500 (full) mV   */
    if (lvl < 0) lvl = 0;
    return 120000 + lvl * 20;              /* ~136k empty .. ~290k full          */
}

void tbd_physics_register(I2CSlave *aw, I2CSlave *ina, I2CSlave *lm, I2CSlave *ioexp)
{
    s_phys_aw    = &TBD_AW9523(aw)->core;
    s_phys_ina   = &TBD_INA226(ina)->core;
    s_phys_lm    = &TBD_LM75B(lm)->core;
    s_phys_ioexp = ioexp ? TBD_IOEXP(ioexp) : NULL;
    s_phys_temp_mc = 22000; s_phys_level_mv = 1000;
    s_phys_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, tbd_physics_tick, NULL);
    timer_mod(s_phys_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 500);
}
