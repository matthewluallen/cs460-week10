#!/usr/bin/env python3
"""Compile the actual UART RX functions against deterministic QEMU boundaries.

This tests model timing/queues, not QEMU scheduling or electrical UART fidelity.
Run the same-app live RTU probe separately to check the integrated model.
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
source = (root / 'hw_char/esp32_uart.c').read_text()
header = (root / 'include/esp32_uart.h').read_text()


def function(name):
    match = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', source, re.M)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


shim = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NANOSECONDS_PER_SECOND 1000000000LL
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define QEMU_CLOCK_VIRTUAL 0
#define OBJECT_CHECK(type,obj,name) ((type *)(obj))
#define REG32(name,addr) enum { A_##name=addr, R_##name=(addr)/4 };
#define FIELD(reg,name,shift,width) enum { R_##reg##_##name##_SHIFT=shift }; \
    static const uint32_t R_##reg##_##name##_MASK=((1ULL<<width)-1)<<shift;
#define FIELD_DP32(v,reg,name,field) (((v)&~R_##reg##_##name##_MASK) | \
    (((uint32_t)(field)<<R_##reg##_##name##_SHIFT)&R_##reg##_##name##_MASK))
typedef int SysBusDevice, SysBusDeviceClass, MemoryRegion, CharBackend;
typedef int MemoryRegionOps, qemu_irq, ResetType;
typedef unsigned guint;
typedef uint64_t hwaddr;
typedef void Object;
typedef struct { int64_t expire_time; bool pending; } QEMUTimer;
typedef struct { uint8_t data[128]; unsigned head,count; } Fifo8;
static int64_t now_ns;
static unsigned accepted, checks;
static unsigned fifo8_num_used(Fifo8 *f) { return f->count; }
static unsigned fifo8_num_free(Fifo8 *f) { return 128-f->count; }
static void fifo8_push(Fifo8 *f,uint8_t b) { assert(f->count<128);f->data[(f->head+f->count++)%128]=b; }
static uint8_t fifo8_pop(Fifo8 *f) { assert(f->count);uint8_t b=f->data[f->head];f->head=(f->head+1)%128;f->count--;return b; }
static void fifo8_reset(Fifo8 *f) { f->head=f->count=0; }
static void timer_del(QEMUTimer *t) { t->pending=false;t->expire_time=-1; }
static void timer_mod_ns(QEMUTimer *t,int64_t ns) { t->pending=true;t->expire_time=ns; }
static int64_t qemu_clock_get_ns(int clock) { (void)clock;return now_ns; }
static void qemu_chr_fe_accept_input(CharBackend *c) { (void)c;accepted++; }
static void qemu_set_irq(qemu_irq i,bool b) { (void)i;(void)b; }
static void qemu_irq_lower(qemu_irq i) { (void)i; }
static void g_source_remove(guint i) { (void)i; }
#define CHECK(c) do { checks++;if(!(c)) { fprintf(stderr,"check failed at %d: %s\n",__LINE__,#c);return 1;} } while(0)
'''
body = '\n'.join(function(n) for n in (
    'esp32_uart_update_irq', 'esp32_uart_set_rx_timeout', 'uart_char_time_ns',
    'uart_receive', 'uart_can_receive', 'uart_throttle_timer_cb',
    'uart_rx_timeout_timer_cb', 'esp32_uart_reset_hold'))
# Exercise the exact production INT_CLR case without stubbing unrelated TX/MMIO.
clear = source.split('    case A_UART_INT_CLR:', 1)[1].split('        break;', 1)[0]
body += '\nstatic void clear_irq(ESP32UARTState *s, uint32_t value) {\n'
body += '    unsigned addr=A_UART_INT_CLR;\n' + clear + '\n    esp32_uart_update_irq(s);\n}\n'
tests = r'''
int main(void) {
    ESP32UARTState s={0};
    uint8_t input[UART_INGRESS_LENGTH];
    for(unsigned i=0;i<sizeof(input);i++) input[i]=(uint8_t)i;
    esp32_uart_reset_hold(&s,0);
    s.baud_rate=19200; s.rx_tout_ena=true;s.rx_tout_thres=20;
    const int64_t tick=520833;
    CHECK(uart_char_time_ns(&s)==tick);
    CHECK(uart_can_receive(&s)==UART_INGRESS_LENGTH);
    uart_receive(&s,input,8);
    CHECK(s.rx_ingress_count==8 && s.rx_fifo.count==0);
    CHECK(uart_can_receive(&s)==UART_INGRESS_LENGTH-8);
    CHECK(s.throttle_timer.expire_time==tick);
    now_ns=tick-1; uart_throttle_timer_cb(&s);
    CHECK(s.rx_fifo.count==0 && s.rx_ingress_count==8);
    now_ns=tick; uart_throttle_timer_cb(&s);
    CHECK(s.rx_fifo.count==1 && s.rx_ingress_count==7);
    CHECK(fifo8_pop(&s.rx_fifo)==0);
    CHECK(s.throttle_timer.expire_time==2*tick);
    now_ns=5*tick+17; uart_throttle_timer_cb(&s);
    CHECK(s.rx_fifo.count==4 && s.rx_ingress_count==3);
    CHECK(s.throttle_timer.expire_time==6*tick);
    for(int i=1;i<5;i++) CHECK(fifo8_pop(&s.rx_fifo)==i);
    uart_receive(&s,input+8,3);
    CHECK(s.rx_ingress_count==6 && s.rx_next_ns==6*tick);
    now_ns=11*tick; uart_throttle_timer_cb(&s);
    CHECK(s.rx_fifo.count==6 && s.rx_ingress_count==0);
    for(int i=5;i<11;i++) CHECK(fifo8_pop(&s.rx_fifo)==i);
    CHECK(!s.throttle_rx && !s.throttle_timer.pending);
    CHECK(s.rx_timeout_timer.pending && s.rx_timeout_timer.expire_time>now_ns);
    uart_rx_timeout_timer_cb(&s);
    CHECK(s.rxfifo_tout);
    clear_irq(&s,R_UART_INT_CLR_RXFIFO_TOUT_MASK);
    CHECK(!s.rxfifo_tout);
    /* New host write after a real idle interval starts a fresh character. */
    now_ns+=100*tick;uart_receive(&s,input,1);
    CHECK(s.rx_next_ns==now_ns+tick);
    now_ns+=tick;uart_throttle_timer_cb(&s);CHECK(fifo8_pop(&s.rx_fifo)==0);
    /* Large host burst: bounded transport, distinct 128-byte hardware FIFO. */
    uart_receive(&s,input,sizeof(input));
    CHECK(uart_can_receive(&s)==0 && s.rx_ingress_count==UART_INGRESS_LENGTH);
    now_ns+=tick*UART_INGRESS_LENGTH;uart_throttle_timer_cb(&s);
    CHECK(s.rx_fifo.count==128 && !s.rx_ingress_count);
    CHECK(s.reg[R_UART_INT_RAW]&R_UART_INT_RAW_RXFIFO_OVF_MASK);
    for(int i=0;i<128;i++) CHECK(fifo8_pop(&s.rx_fifo)==i);
    clear_irq(&s,R_UART_INT_CLR_RXFIFO_OVF_MASK);
    CHECK(!(s.reg[R_UART_INT_RAW]&R_UART_INT_RAW_RXFIFO_OVF_MASK));
    /* Ring wrap, sustained consumption, and idle boundaries preserve ordering. */
    for(int burst=0;burst<40;burst++) {
        uart_receive(&s,input,255);
        for(int i=0;i<255;i++) {
            now_ns+=tick;uart_throttle_timer_cb(&s);
            CHECK(s.rx_fifo.count==1 && fifo8_pop(&s.rx_fifo)==i);
        }
        CHECK(!s.rx_ingress_count && !s.throttle_rx);
    }
    /* Reset mid-frame cannot leak queued bytes or a stale timeout. */
    uart_receive(&s,input,16);s.rxfifo_tout=true;
    CHECK(s.throttle_timer.pending && s.rx_timeout_timer.pending);
    esp32_uart_reset_hold(&s,0);
    CHECK(!s.rx_ingress_count && !s.rx_ingress_head && !s.rx_next_ns);
    CHECK(!s.rx_fifo.count && !s.tx_fifo.count && !s.rxfifo_tout);
    CHECK(!s.throttle_timer.pending && !s.rx_timeout_timer.pending);
    now_ns+=100*tick;uart_throttle_timer_cb(&s);
    CHECK(!s.rx_fifo.count && uart_can_receive(&s)==UART_INGRESS_LENGTH);
    printf("%u UART model boundary checks passed\n",checks);
    return 0;
}
'''
c = a.out / 'uart_rx.c'
c.write_text(shim + '\n' + re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M) + '\n' + body + tests)
exe = a.out / 'uart_rx'
san = os.environ.get('SANITIZERS', 'undefined' if os.uname().sysname == 'Darwin' else 'address,undefined')
cmd = [os.environ.get('CC', 'cc'), '-std=c11', '-g', '-Wall', '-Wextra',
       '-Wno-unused-parameter', '-Wno-unused-const-variable', '-Werror',
       '-fsanitize=' + san, '-fno-omit-frame-pointer', str(c), '-o', str(exe)]
subprocess.run(cmd, check=True)
result = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
(a.out / 'summary.json').write_text(json.dumps({
    'result': result.stdout.strip(), 'sanitizers': san,
    'scope': 'actual RX functions; deterministic host clock/FIFO/timer boundaries',
    'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
    'header_sha256': hashlib.sha256(header.encode()).hexdigest(),
    'compile': cmd}, indent=2) + '\n')
print(result.stdout, end='')
