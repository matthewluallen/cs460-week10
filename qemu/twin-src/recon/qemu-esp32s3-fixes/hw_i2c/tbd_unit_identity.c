/* TBD MAX30100/SGP30/MLX90614/VL53L1X protocol fixtures. GPL-2.0-or-later.
 * Synthetic optical/gas values exercise firmware timing and quality, not
 * physiology, air chemistry or calibration. Device identity traffic is retained.
 */
#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#define TYPE_TBD_UNIT_ID "tbd-unit-id"
OBJECT_DECLARE_SIMPLE_TYPE(TbdUnitId, TBD_UNIT_ID)
struct TbdUnitId {
    I2CSlave parent;
    uint8_t kind, pointer, phase, response[9], size, offset, revision;
    uint16_t command;
    int64_t ready_ms, reset_ms, sample_ms, warmup_ms, measure_ms;
    bool crc_error, measure_crc_error, force_overflow, force_burst, burst_done, initialized;
    unsigned burst_read_bytes;
    uint8_t regs[256], fifo[16][4], fifo_offset, count;
    uint32_t sequence;
    uint16_t ncir_ambient, ncir_object;
    uint64_t ncir_serial;
    bool ncir_sensor_error;
    uint16_t tof_model, tof_distance;
    bool tof_ranging, tof_never_ready;
    uint8_t tof_status;
};
static uint8_t unit_crc(const uint8_t *p) {
    uint8_t c = 0xff;
    for (int i=0;i<2;i++) { c ^= p[i]; for(int b=0;b<8;b++) c = (c & 0x80) ? (c << 1) ^ 0x31 : c << 1; }
    return c;
}
/* Default-address, read-only MLX90614 SMBus fixture. No EEPROM writes,
 * calibration, infrared physics, conversion timing or arbitrary addressing.
 * PEC is independent of the firmware decoder; address bytes are part of it. */
static uint8_t ncir_pec(uint8_t command, uint16_t word) {
    uint8_t bytes[] = {0xb4, command, 0xb5, word & 0xff, word >> 8};
    uint8_t crc = 0;
    for (unsigned i = 0; i < sizeof(bytes); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
    }
    return crc;
}
static int ncir_command(TbdUnitId *d, uint8_t command) {
    uint16_t word;
    bool measurement = command == 6 || command == 7;
    if (command == 6) word = d->ncir_ambient;
    else if (command == 7) word = d->ncir_object;
    else if (command == 0x2e) word = 0x5a;
    else if (command >= 0x3c && command <= 0x3f)
        word = d->ncir_serial >> (16 * (0x3f - command));
    else return -1;
    if (measurement && d->ncir_sensor_error) word |= 0x8000;
    d->response[0] = word;
    d->response[1] = word >> 8;
    d->response[2] = ncir_pec(command, word);
    if (measurement ? d->measure_crc_error : d->crc_error)
        d->response[2] ^= 1;
    d->size = 3;
    return 0;
}
static void heart_update(TbdUnitId *d) {
    int64_t now=qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    if(d->regs[6]&0x40) {
        if(now>=d->reset_ms) { memset(d->regs,0,sizeof(d->regs));d->count=d->fifo_offset=0; }
        return;
    }
    if((d->regs[6]&0x83)!=3) {d->sample_ms=now;return;}
    int64_t n=(now-d->sample_ms)/20;
    if(n>32) {d->regs[3]=15;d->sample_ms=now-640;n=32;}
    while(n-->0) {
        d->sample_ms+=20;
        if(d->count==16) {d->regs[3]=MIN(15,d->regs[3]+1);continue;}
        uint8_t *p=d->fifo[d->regs[2]&15];uint16_t ir=2000+(d->sequence%100),red=1100+(d->sequence%100);
        p[0]=ir>>8;p[1]=ir;p[2]=red>>8;p[3]=red;
        ++d->sequence;++d->count;d->regs[2]=(d->regs[2]+1)&15;
    }
    if(d->force_overflow)d->regs[3]=3;
}
static int unit_event(I2CSlave *s, enum i2c_event e) {
    TbdUnitId *d = TBD_UNIT_ID(s);
    if(d->kind==3) {
        if(e==I2C_START_SEND || e==I2C_FINISH){d->phase=0;d->command=0;}
        else if(e==I2C_START_RECV){
            if(d->phase!=2 || (!d->tof_ranging && d->command!=0x010f && d->command!=0x0110))return -1;
            d->phase=3;
        }
        return 0;
    }
    if (d->kind == 2) {
        if (e == I2C_START_SEND || e == I2C_FINISH) {
            d->phase = d->size = d->offset = 0;
        } else if (e == I2C_START_RECV) {
            if (d->phase != 1 || d->size != 3) return -1;
            d->phase = 2; d->offset = 0;
        }
        return 0;
    }
    if(d->kind==0) {
        if(e==I2C_START_RECV && d->pointer==2 && d->force_burst && !d->burst_done && (d->regs[6]&0x83)==3) {
            // Inject a known 12-frame history at the status-read boundary.
            d->sample_ms=qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL)-240;
            d->count=0;d->regs[2]=d->regs[4]=0;d->burst_done=true;
        }
        heart_update(d);
    }
    if(e == I2C_START_SEND) { d->phase=0; d->command=0; }
    if(e == I2C_START_RECV) {
        d->offset=0;d->fifo_offset=0;
        if(d->kind == 1 && (!d->size || qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) < d->ready_ms)) return -1;
    }
    return 0;
}
static int unit_send(I2CSlave *s, uint8_t v) {
    TbdUnitId *d=TBD_UNIT_ID(s);
    int64_t now=qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    if(d->kind==3) {
        if(d->phase<2){d->command=(d->command<<8)|v;++d->phase;return 0;}
        if(!d->tof_ranging || (d->phase!=2 && d->phase!=4)) {d->phase=5;return -1;}
        uint16_t reg=d->command++;
        if(!(reg==0 || reg==8 || reg==0x0b || (reg>=0x2d && reg<=0x87))){d->phase=5;return -1;}
        d->phase=4;d->regs[reg]=v;
        if(reg==0){
            d->initialized=false;d->reset_ms=now+2;
            if(!v)memset(d->regs,0,sizeof(d->regs));
        }
        if(reg==0x87){d->initialized=v==0x40;d->ready_ms=now+100;}
        if(reg==0x86 && (v&1))d->ready_ms=now+120;
        return 0;
    }
    if (d->kind == 2) {
        if (d->phase != 0) { d->phase = 3; d->size = 0; return -1; }
        d->phase = 1;
        return ncir_command(d, v);
    }
    if(d->kind == 0) {
        if(!d->phase++) {d->pointer=v;return 0;}
        uint8_t r=d->pointer++;d->regs[r]=v;
        if(r==6) {d->sample_ms=now;if(v&0x40)d->reset_ms=now+5;}
        if(r==4) {d->count=0;d->fifo_offset=0;}
        return 0;
    }
    if(d->phase >= 2) return -1;
    d->command=(d->command << 8)|v;
    if(++d->phase == 2) {
        memset(d->response,0,sizeof(d->response)); d->size=0;
        bool identity=true;
        if(d->command == 0x202f) { d->size=3;d->response[1]=0x22; }
        else if(d->command == 0x3682) { d->size=9;for(int i=0;i<9;i+=3){d->response[i]=i+1;d->response[i+1]=i+2;} }
        else if(d->command == 0x2003) {d->initialized=true;d->warmup_ms=now;d->measure_ms=0;d->ready_ms=now+10;return 0;}
        else if(d->command == 0x2008) {
            if(!d->initialized || now<d->ready_ms)return -1;
            if(d->measure_ms && (now-d->measure_ms<750 || now-d->measure_ms>1500))d->warmup_ms=now;
            d->measure_ms=now;identity=false;d->size=6;
            unsigned eco2=now-d->warmup_ms<15000?400:600,tvoc=now-d->warmup_ms<15000?0:20;
            d->response[0]=eco2>>8;d->response[1]=eco2;d->response[3]=tvoc>>8;d->response[4]=tvoc;
        } else return -1;
        for(int i=0;i<d->size;i+=3) d->response[i+2]=unit_crc(d->response+i);
        if((identity&&d->crc_error)||(!identity&&d->measure_crc_error))d->response[d->size-1]^=1;
        d->ready_ms=now+(identity?10:12);
    }
    return 0;
}
static uint8_t unit_recv(I2CSlave *s) {
    TbdUnitId *d=TBD_UNIT_ID(s);
    if(d->kind==3) {
        if(d->phase!=3)return 0xff;
        uint16_t reg=d->command++;
        if(reg==0x010f)return d->tof_model>>8;
        if(reg==0x0110)return d->tof_model&0xff;
        if(!d->tof_ranging)return 0xff;
        int64_t now=qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
        bool ready=d->initialized && !d->tof_never_ready && now>=d->ready_ms;
        if(reg==0xe5)return d->regs[0] && now>=d->reset_ms;
        if(reg==0xde)return 0;
        if(reg==0xdf)return 100; // synthetic oscillator calibration, not wire timing
        if(reg==0x31)return ready?!(d->regs[0x30]&0x10):!!(d->regs[0x30]&0x10);
        if(reg>=0x89 && reg<=0x99){
            if(!ready)return 0xff;
            if(reg==0x89)return d->tof_status;
            if(reg==0x96)return d->tof_distance>>8;
            if(reg==0x97)return d->tof_distance&0xff;
            return 0;
        }
        return reg<256?d->regs[reg]:0xff;
    }
    if (d->kind == 2 && d->phase != 2) return 0xff;
    if(d->kind == 0) {
        if(d->pointer==5) {
            if(!d->count)return 0;
            uint8_t v=d->fifo[d->regs[4]&15][d->fifo_offset++];
            if(d->force_burst && d->burst_read_bytes<48 && ++d->burst_read_bytes==48)
                fprintf(stderr,"TBD FIFO burst consumed: 48 bytes\n");
            if(d->fifo_offset==4){d->fifo_offset=0;d->regs[4]=(d->regs[4]+1)&15;--d->count;}
            return v;
        }
        uint8_t p=d->pointer++;return p==0xff?0x11:p==0xfe?d->revision:d->regs[p];
    }
    return d->offset<d->size?d->response[d->offset++]:0xff;
}
static Property unit_props[]={
    DEFINE_PROP_BOOL("tof-ranging",TbdUnitId,tof_ranging,false),
    DEFINE_PROP_BOOL("tof-never-ready",TbdUnitId,tof_never_ready,false),
    DEFINE_PROP_UINT8("tof-status",TbdUnitId,tof_status,9),
    DEFINE_PROP_UINT16("tof-distance",TbdUnitId,tof_distance,500),
    DEFINE_PROP_UINT16("tof-model",TbdUnitId,tof_model,0xeacc),
    DEFINE_PROP_UINT8("kind",TbdUnitId,kind,0),
    DEFINE_PROP_UINT8("revision",TbdUnitId,revision,3),
    DEFINE_PROP_BOOL("crc-error",TbdUnitId,crc_error,false),
    DEFINE_PROP_BOOL("measure-crc-error",TbdUnitId,measure_crc_error,false),
    DEFINE_PROP_BOOL("force-overflow",TbdUnitId,force_overflow,false),
    DEFINE_PROP_BOOL("force-burst",TbdUnitId,force_burst,false),
    DEFINE_PROP_UINT16("ncir-ambient-raw",TbdUnitId,ncir_ambient,15000),
    DEFINE_PROP_UINT16("ncir-object-raw",TbdUnitId,ncir_object,15100),
    DEFINE_PROP_UINT64("ncir-serial",TbdUnitId,ncir_serial,0x0011223344556677ULL),
    DEFINE_PROP_BOOL("ncir-sensor-error",TbdUnitId,ncir_sensor_error,false),
    DEFINE_PROP_END_OF_LIST()
};
static void unit_class(ObjectClass *klass,void *data) {
    I2CSlaveClass *k=I2C_SLAVE_CLASS(klass);
    k->event=unit_event;k->send=unit_send;k->recv=unit_recv;
    device_class_set_props(DEVICE_CLASS(klass),unit_props);
}
static const TypeInfo info={.name=TYPE_TBD_UNIT_ID,.parent=TYPE_I2C_SLAVE,.instance_size=sizeof(TbdUnitId),.class_init=unit_class};
static void unit_register(void){type_register_static(&info);}
type_init(unit_register)
