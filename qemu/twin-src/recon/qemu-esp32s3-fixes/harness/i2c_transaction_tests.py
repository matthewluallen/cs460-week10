#!/usr/bin/env python3
"""Execute actual I2C controller functions at deterministic bus/FIFO boundaries.

FR20 acceptance starts with expected-ACK comparison and failure recovery. These
tests do not establish wire timing or a live firmware's response to a model NACK.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=False)
root = Path(__file__).resolve().parents[1]
source = (root/'hw_i2c/esp32_i2c.c').read_text()
header = (root/'include/esp32_i2c.h').read_text()
body = source.split('static const MemoryRegionOps', 1)[0]
shim = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define OBJECT(s) ((void *)(s))
static char *object_get_canonical_path(void *s) { (void)s; char *p=malloc(32); strcpy(p,"/machine/soc/i2c0"); return p; }
#define g_free(p) free(p)
#define OBJECT_CHECK(type,obj,name) ((type *)(obj))
#define REG32(name,addr) enum { A_##name=addr, R_##name=(addr)/4 };
#define FIELD(reg,name,shift,width) enum { R_##reg##_##name##_SHIFT=shift }; \
    static const uint32_t R_##reg##_##name##_MASK=((1ULL<<width)-1)<<shift;
#define FIELD_DP32(v,reg,name,field) (((v)&~R_##reg##_##name##_MASK) | \
    (((uint32_t)(field)<<R_##reg##_##name##_SHIFT)&R_##reg##_##name##_MASK))
#define FIELD_EX32(v,reg,name) (((v)&R_##reg##_##name##_MASK)>>R_##reg##_##name##_SHIFT)
typedef int SysBusDevice, MemoryRegion, ResetType;
typedef unsigned *qemu_irq;
typedef void Object;
typedef uint64_t hwaddr;
typedef struct { uint8_t data[32]; unsigned head,count; } Fifo8;
typedef struct { unsigned starts,sends,ends,recv; int address_nack,data_nack; bool active; uint8_t sent[1024]; } I2CBus;
static unsigned checks, reports, irq;
#define QEMU_CLOCK_VIRTUAL 0
#define RESET_TYPE_COLD 0
static int64_t qemu_clock_get_ns(int clock) { (void)clock; return 123456789; }
typedef struct { void *controller; uint64_t seq,epoch; int64_t time; const char *event; int address,direction,data,nack; } Observation;
static Observation observations[4096];
static unsigned observation_count;
static void trace_esp32_i2c_observe(void *controller,const char *path,uint64_t seq,uint64_t epoch,
                                  int64_t time,const char *event,int address,
                                  int direction,int data,int nack) {
    assert(strcmp(path,"/machine/soc/i2c0")==0);
    assert(observation_count<4096);
    observations[observation_count++]=(Observation){controller,seq,epoch,time,event,address,direction,data,nack};
}

static unsigned fifo8_num_used(Fifo8 *f) { return f->count; }
static unsigned fifo8_num_free(Fifo8 *f) { return 32-f->count; }
static void fifo8_push(Fifo8 *f,uint8_t b) { assert(f->count<32);f->data[(f->head+f->count++)%32]=b; }
static uint8_t fifo8_pop(Fifo8 *f) { assert(f->count);uint8_t b=f->data[f->head];f->head=(f->head+1)%32;f->count--;return b; }
static void fifo8_reset(Fifo8 *f) { f->head=f->count=0; }
static void route_reset_irq(qemu_irq line,int level);
static void qemu_set_irq(qemu_irq line,int level) { if(line)*line=!!level;route_reset_irq(line,level); }
static void qemu_irq_lower(qemu_irq line) { qemu_set_irq(line,0); }
#define BIT(n) (1U<<(n))
typedef int SysBusDeviceClass;

static bool i2c_bus_busy(I2CBus *b) { return b->active; }
static int i2c_start_transfer(I2CBus *b,uint8_t addr,bool read) { (void)addr;(void)read;b->starts++;b->active=!b->address_nack;return b->address_nack; }
static int i2c_send(I2CBus *b,uint8_t data) { assert(b->sends<1024);b->sent[b->sends++]=data;return b->data_nack; }
static uint8_t i2c_recv(I2CBus *b) { return b->recv++; }
static void i2c_end_transfer(I2CBus *b) { b->ends++;b->active=false; }
#define error_report(...) do { reports++; } while(0)
#define CHECK(v) do { checks++;if(!(v)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#v);return 1;} } while(0)
'''
clock_source=(root/'hw_xtensa/esp32s3_clk.c').read_text()
clock_header=(root/'include/esp32s3_clk.h').read_text()
clock_defs=(root/'include/esp32s3_clk_defs.h').read_text()
clock_body=clock_source.split('static const MemoryRegionOps',1)[0]
clock_body+=clock_source[clock_source.index('static void esp32s3_clock_reset_hold('):clock_source.index('static void esp32s3_clock_realize(')]
tests = r'''
static unsigned reset_lines[2];
static Esp32I2CState *reset_targets[2];
static void route_reset_irq(qemu_irq line,int level) {
    for(unsigned i=0;i<2;i++)
        if(line==&reset_lines[i] && reset_targets[i])
            esp32_i2c_peripheral_reset(reset_targets[i],0,level);
}

static void fresh(Esp32I2CState *s,I2CBus *b) {
    observation_count=0;
    memset(s,0,sizeof(*s));memset(b,0,sizeof(*b));s->bus=b;s->irq=&irq;
    s->observe_address=s->observe_read=-1;
    s->s3_mode=true;esp32_i2c_reset_hold(s,0);irq=0;
}
static uint32_t write_cmd(unsigned count,bool check,bool expected) {
    return (1<<11)|(check<<8)|(expected<<9)|count;
}
static void prepare(Esp32I2CState *s,bool check,bool expected) {
    fifo8_push(&s->tx_fifo,0xee);fifo8_push(&s->tx_fifo,0x10);fifo8_push(&s->tx_fifo,0x42);
    s->cmd_reg[0]=write_cmd(3,check,expected);s->cmd_reg[1]=2<<11; /* S3 STOP */
    s->int_ena_reg=1<<10;
}
int main(void) {
    Esp32I2CState s;I2CBus b;
    fresh(&s,&b);CHECK(s.fifo_conf_reg==(11|(4<<5)|(1<<14)));
    /* Peripheral reset clears selected routing and suspends MMIO until release;
     * repeated assertion does not repeatedly reset the peripheral. */
    fresh(&s,&b);b.active=true;s.trans_ongoing=false;
    fifo8_push(&s.tx_fifo,0x42);s.int_raw_reg=s.int_ena_reg=1;irq=1;
    s.read_pending=s.write_pending=true;s.read_remaining=s.write_remaining=4;
    esp32_i2c_peripheral_reset(&s,0,1);
    CHECK(s.peripheral_reset && !b.active && b.ends==1 && irq==0);
    CHECK(!s.read_pending && !s.write_pending && !s.read_remaining && !s.write_remaining);
    CHECK(fifo8_num_used(&s.tx_fifo)==0 && fifo8_num_used(&s.rx_fifo)==0);
    uint64_t reset_epoch=s.observe_epoch;
    esp32_i2c_peripheral_reset(&s,0,1);CHECK(s.observe_epoch==reset_epoch && b.ends==1);
    esp32_i2c_write(&s,A_I2C_FIFO_DATA,0x66,4);
    esp32_i2c_write(&s,A_I2C_CTR,(1<<4)|(1<<5),4);
    CHECK(!b.starts && !fifo8_num_used(&s.tx_fifo));
    CHECK(esp32_i2c_read(&s,A_I2C_FIFO_DATA,4)==0);
    esp32_i2c_peripheral_reset(&s,0,0);CHECK(!s.peripheral_reset);
    prepare(&s,true,false);esp32_i2c_do_transaction(&s);
    CHECK(b.starts==1 && b.sends==2 && !b.active);
    /* SMBus command + repeated START must not deliver a STOP to the slave. */
    fresh(&s,&b);unsigned ends_before=b.ends;
    fifo8_push(&s.tx_fifo,0xb4);fifo8_push(&s.tx_fifo,7);fifo8_push(&s.tx_fifo,0xb5);
    s.cmd_reg[0]=6<<11;s.cmd_reg[1]=write_cmd(2,true,false);
    s.cmd_reg[2]=6<<11;s.cmd_reg[3]=write_cmd(1,true,false);
    s.cmd_reg[4]=(3<<11)|3;s.cmd_reg[5]=2<<11;
    esp32_i2c_do_transaction(&s);
    CHECK(b.ends==ends_before+1);CHECK(b.starts==2 && b.sends==1 && b.sent[0]==7);
    CHECK(b.recv==3 && !b.active && !s.trans_ongoing);
    /* RSTART can leave a selected slave while waiting for a new address. */
    for(int reset=0;reset<2;reset++) {
        fresh(&s,&b);b.active=true;s.trans_ongoing=false;CHECK(i2c_bus_busy(&b));
        CHECK(FIELD_EX32(esp32_i2c_get_status_reg(&s),I2C_STATUS,BUS_BUSY));
        if(reset)esp32_i2c_write(&s,A_I2C_CTR,(1<<10)|R_I2C_CTR_MS_MODE_MASK,4);
        else esp32_i2c_reset_hold(&s,0);
        CHECK(!b.active && !s.trans_ongoing);
    }
    s.s3_mode=false;esp32_i2c_reset_hold(&s,0);
    CHECK(s.fifo_conf_reg==(11|(4<<5))); /* Preserve original-controller reset. */
    for(int enabled=0;enabled<2;enabled++) for(int expected=0;expected<2;expected++)
    for(int address=0;address<2;address++) for(int data=0;data<2;data++) {
        fresh(&s,&b);prepare(&s,enabled,expected);b.address_nack=address;b.data_nack=data;
        esp32_i2c_do_transaction(&s);
        bool mismatch=enabled && (address!=expected || data!=expected);
        CHECK(!!(s.int_raw_reg&(1<<10))==mismatch);
        CHECK(!!irq==mismatch);
        CHECK(!s.trans_ongoing && !b.active);
        CHECK(b.starts==1);
        if(mismatch) {
            CHECK(!(s.cmd_reg[0]&(1u<<31))); /* incomplete WRITE */
            CHECK(!(s.cmd_reg[1]&(1u<<31))); /* trailing STOP not executed */
            CHECK(b.sends==(address!=expected?0u:1u));
            CHECK(!s.read_pending && s.resume_cmd==0);
        } else {
            CHECK(b.sends==2);
            CHECK((s.cmd_reg[0]&(1u<<31)) && (s.cmd_reg[1]&(1u<<31)));
            CHECK(s.int_raw_reg&(1<<7));
        }
    }
    /* An acknowledged next address must not silently clear a sticky NACK. */
    fresh(&s,&b);s.int_raw_reg=1<<10;prepare(&s,true,false);
    esp32_i2c_do_transaction(&s);CHECK(s.int_raw_reg&(1<<10));
    esp32_i2c_write(&s,A_I2C_INT_CLR,1<<10,4);CHECK(!(s.int_raw_reg&(1<<10)) && !irq);
    /* A following request recovers after failed data; no stale slave/command. */
    fresh(&s,&b);prepare(&s,true,false);b.data_nack=1;
    esp32_i2c_do_transaction(&s);CHECK(s.int_raw_reg&(1<<10));
    esp32_i2c_write(&s,A_I2C_INT_CLR,0xffffffffu,4);fifo8_reset(&s.tx_fifo);
    b.data_nack=0;prepare(&s,true,false);esp32_i2c_do_transaction(&s);
    CHECK(!(s.int_raw_reg&(1<<10)) && !s.trans_ongoing && !b.active);
    CHECK(s.cmd_reg[1]&(1u<<31));
    /* S3 implements eight commands; parent ESP32 has sixteen. MMIO after the
     * last command must not read/write the array or an adjacent object. */
    fresh(&s,&b);s.cmd_reg[7]=0x12345678;s.cmd_reg[8]=0x87654321;
    CHECK(esp32_i2c_read(&s,A_I2C_CMD+7*4,4)==0x12345678);
    esp32_i2c_write(&s,A_I2C_CMD+8*4,0,4);CHECK(s.cmd_reg[8]==0x87654321);
    CHECK(esp32_i2c_read(&s,A_I2C_CMD+8*4,4)==0);
    s.s3_mode=false;s.cmd_reg[15]=0x11223344;
    CHECK(esp32_i2c_read(&s,A_I2C_CMD+15*4,4)==0x11223344);
    CHECK(esp32_i2c_read(&s,A_I2C_CMD+16*4,4)==0);
    esp32_i2c_write(&s,A_I2C_CMD+16*4,0,4);
    CHECK(s.cmd_reg[15]==0x11223344);
    /* Empty address/data FIFO is pending, not a fabricated NACK or DONE. */
    fresh(&s,&b);s.cmd_reg[0]=write_cmd(3,true,false);s.cmd_reg[1]=2<<11;
    s.int_ena_reg=(1<<1)|(1<<6);esp32_i2c_do_transaction(&s);
    CHECK(s.write_pending && s.write_remaining==3 && !s.trans_ongoing);
    CHECK(!b.starts && !b.sends && !(s.int_raw_reg&(1<<10)));
    CHECK((s.int_raw_reg&(1<<6)) && irq && !(s.cmd_reg[0]&(1u<<31)));
    esp32_i2c_write(&s,A_I2C_FIFO_DATA,0xee,4);
    CHECK(b.starts==1 && s.trans_ongoing && s.write_pending && s.write_remaining==2);
    esp32_i2c_write(&s,A_I2C_FIFO_DATA,0x10,4);
    CHECK(b.sends==1 && s.write_pending && s.write_remaining==1);
    esp32_i2c_write(&s,A_I2C_FIFO_DATA,0x42,4);
    CHECK(b.sends==2 && b.sent[0]==0x10 && b.sent[1]==0x42);
    CHECK(!s.write_pending && !s.write_remaining && !s.trans_ongoing && !b.active);
    CHECK((s.cmd_reg[0]&(1u<<31)) && (s.cmd_reg[1]&(1u<<31)));
    esp32_i2c_write(&s,A_I2C_INT_CLR,0xffffffffu,4);CHECK(!irq);
    /* Maximum-byte command across repeated one-byte refills preserves order. */
    fresh(&s,&b);s.cmd_reg[0]=write_cmd(255,true,false);s.cmd_reg[1]=2<<11;
    fifo8_push(&s.tx_fifo,0xee);
    for(int i=0;i<31;i++)fifo8_push(&s.tx_fifo,i);
    esp32_i2c_do_transaction(&s);CHECK(s.write_pending && s.write_remaining==223 && b.sends==31);
    for(int i=31;i<254;i++) {
        CHECK(!(s.cmd_reg[0]&(1u<<31)));
        esp32_i2c_write(&s,A_I2C_FIFO_DATA,i,4);
    }
    CHECK(b.starts==1 && b.sends==254 && !s.write_pending && !s.trans_ongoing);
    for(int i=0;i<254;i++)CHECK(b.sent[i]==i);
    /* Unsupported zero length cannot pop an empty FIFO or wrap a byte count. */
    fresh(&s,&b);s.cmd_reg[0]=write_cmd(0,true,false);unsigned old_reports=reports;
    esp32_i2c_do_transaction(&s);
    CHECK(reports==old_reports+1 && !b.starts && !b.sends);
    CHECK(!s.write_pending && !(s.cmd_reg[0]&(1u<<31)) && !(s.int_raw_reg&(1<<10)));
    /* Reset after a partial data phase releases the slave and cannot resume it. */
    fresh(&s,&b);s.cmd_reg[0]=write_cmd(3,true,false);fifo8_push(&s.tx_fifo,0xee);
    esp32_i2c_do_transaction(&s);CHECK(s.write_pending && b.active);
    esp32_i2c_write(&s,A_I2C_CTR,(1<<4)|(1<<10),4);
    CHECK(!s.write_pending && !s.trans_ongoing && !b.active && !s.write_remaining);
    CHECK(!(esp32_i2c_read(&s,A_I2C_CTR,4)&(1<<10)));
    esp32_i2c_write(&s,A_I2C_FIFO_DATA,0x55,4);CHECK(!b.sends && s.tx_fifo.count==1);
    s.trans_ongoing=true;b.active=true;s.int_ena_reg=1<<6;s.int_raw_reg=1<<6;
    esp32_i2c_update_irq(&s);CHECK(irq);
    esp32_i2c_reset_hold(&s,0);
    CHECK(!irq && !b.active && !s.trans_ongoing && !s.write_pending && !s.tx_fifo.count);
    /* A READ crossing RX watermarks continues exactly once per byte. */
    fresh(&s,&b);s.trans_ongoing=true;b.active=true;
    s.cmd_reg[0]=(3<<11)|63;s.cmd_reg[1]=2<<11;s.int_ena_reg=1;
    esp32_i2c_do_transaction(&s);
    CHECK(s.read_pending && s.rx_fifo.count==12 && irq && !(s.cmd_reg[0]&(1u<<31)));
    for(unsigned i=0;i<63;i++)CHECK(esp32_i2c_read(&s,A_I2C_FIFO_DATA,4)==i);
    CHECK(b.recv==63 && !s.read_pending && !s.rx_fifo.count && !s.trans_ongoing);
    CHECK((s.cmd_reg[0]&(1u<<31)) && (s.cmd_reg[1]&(1u<<31)));
    unsigned reads=0;
    for(unsigned i=0;i<observation_count;i++) {
        CHECK(observations[i].seq==i+1 && observations[i].controller==&s);
        CHECK(observations[i].time==123456789 && observations[i].epoch==1);
        if(!strcmp(observations[i].event,"read")) {
            CHECK(observations[i].data==(int)reads++ && observations[i].nack==-1);
        }
    }
    CHECK(reads==63); /* watermark resumes do not duplicate bytes */
    fresh(&s,&b);prepare(&s,true,false);esp32_i2c_do_transaction(&s);
    CHECK(observation_count==5); /* reset/address/two data bytes/stop */
    CHECK(!strcmp(observations[1].event,"address"));
    CHECK(observations[1].address==0x77 && observations[1].direction==0 && observations[1].data==0xee && observations[1].nack==0);
    CHECK(!strcmp(observations[2].event,"write") && observations[2].data==0x10);
    CHECK(!strcmp(observations[3].event,"write") && observations[3].data==0x42);
    CHECK(!strcmp(observations[4].event,"stop"));
    uint64_t before=s.observe_seq;esp32_i2c_reset_hold(&s,0);
    CHECK(s.observe_seq==before+1 && s.observe_epoch==2);
    CHECK(!strcmp(observations[5].event,"reset") && observations[5].epoch==2);
    fresh(&s,&b);prepare(&s,true,false);b.address_nack=1;esp32_i2c_do_transaction(&s);
    CHECK(observation_count==3 && observations[1].nack==1);
    CHECK(!strcmp(observations[2].event,"ack_mismatch_stop"));
    CHECK(!b.sends); /* observer cannot turn failed address into a write */
    /* Clock MMIO drives actual controller reset callbacks through fake IRQ
     * lines; verify target isolation, retained non-I2C bits and global release. */
    Esp32I2CState other;I2CBus otherbus;ESP32S3ClockState clk={0};
    fresh(&other,&otherbus);fresh(&s,&b);
    reset_targets[0]=&s;reset_targets[1]=&other;
    clk.i2c_reset[0]=&reset_lines[0];clk.i2c_reset[1]=&reset_lines[1];
    b.active=otherbus.active=true;
    esp32s3_clock_write(&clk,A_SYSTEM_PERIP_RST_EN0,(1U<<31)|(1U<<7),4);
    CHECK(esp32s3_clock_read(&clk,A_SYSTEM_PERIP_RST_EN0,4)==((1U<<31)|(1U<<7)));
    CHECK(s.peripheral_reset && !other.peripheral_reset && !b.active && otherbus.active);
    CHECK(b.ends==1 && otherbus.ends==0);
    esp32s3_clock_write(&clk,A_SYSTEM_PERIP_RST_EN0,(1U<<31)|(1U<<18),4);
    CHECK(!s.peripheral_reset && other.peripheral_reset && !otherbus.active);
    CHECK(b.ends==1 && otherbus.ends==1);
    CHECK(esp32s3_clock_read(&clk,A_SYSTEM_PERIP_RST_EN0,4)==((1U<<31)|(1U<<18)));
    esp32s3_clock_write(&clk,A_SYSTEM_PERIP_RST_EN0,(1U<<31),4);
    CHECK(!s.peripheral_reset && !other.peripheral_reset);
    CHECK(esp32s3_clock_read(&clk,A_SYSTEM_PERIP_RST_EN0,4)==(1U<<31));
    esp32s3_clock_write(&clk,A_SYSTEM_PERIP_RST_EN0,(1U<<7)|(1U<<18),4);
    CHECK(s.peripheral_reset && other.peripheral_reset);
    esp32s3_clock_reset_hold(&clk,RESET_TYPE_COLD);
    CHECK(!s.peripheral_reset && !other.peripheral_reset && clk.perip_rst_en0==0);
    CHECK(b.ends==1 && otherbus.ends==1); /* no extra STOP when already idle */
    CHECK(esp32s3_clock_get_ext_dev_enc_dec_ctrl(&clk)==0);
    reset_targets[0]=reset_targets[1]=NULL;
    printf("%u I2C ACK/recovery/FIFO/register-boundary and observation checks passed\n",checks);
    return 0;
}
'''
strip = lambda s: re.sub(r'^#(?:include|pragma).*$', '', s, flags=re.M)
c = a.out/'i2c_transaction.c'
c.write_text(shim+'\n'+strip(header)+'\n'+strip(body)+'\n'+strip(clock_header)+'\n'+strip(clock_defs)+'\n'+strip(clock_body)+'\n'+tests)
exe = a.out/'i2c_transaction'
san = os.environ.get('SANITIZERS', 'undefined' if os.uname().sysname == 'Darwin' else 'address,undefined')
cmd = [os.environ.get('CC','cc'),'-std=c11','-g','-Wall','-Wextra','-Werror',
       '-Wno-unused-parameter','-Wno-unused-const-variable',
       '-fsanitize='+san,'-fno-omit-frame-pointer',str(c),'-o',str(exe)]
subprocess.run(cmd,check=True)
run = subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
result = dict(passed=run.returncode==0,returncode=run.returncode,sanitizers=san,
              stdout=run.stdout,stderr=run.stderr,
              source_sha256=hashlib.sha256(source.encode()).hexdigest(),
              header_sha256=hashlib.sha256(header.encode()).hexdigest(),
              clock_source_sha256=hashlib.sha256(clock_source.encode()).hexdigest(),
              clock_header_sha256=hashlib.sha256(clock_header.encode()).hexdigest(),
              clock_defs_sha256=hashlib.sha256(clock_defs.encode()).hexdigest(),
              scope='actual controller; fake bus/FIFO/IRQ boundary; no physical timing or live-app acceptance')
(a.out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
raise SystemExit(run.returncode)
