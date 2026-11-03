#!/usr/bin/env python3
"""Compile actual QEMU model callbacks with a minimal host shim.

Protocol/transaction boundary checks only; not a QEMU build or physical parity.
"""
import argparse, hashlib, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
src=Path(__file__).resolve().parents[1]/'hw_i2c/tbd_unit_identity.c'
text=src.read_text();body=text[text.index('struct TbdUnitId {'):text.index('static Property unit_props')]
shim=r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef int I2CSlave;
typedef struct TbdUnitId TbdUnitId;
enum i2c_event {I2C_START_SEND,I2C_START_RECV,I2C_FINISH,I2C_NACK};
#define TBD_UNIT_ID(s) ((TbdUnitId *)(s))
#define QEMU_CLOCK_VIRTUAL 0
#define MIN(a,b) ((a)<(b)?(a):(b))
static int64_t now;
static int64_t qemu_clock_get_ms(int clock) {(void)clock;return now;}
static unsigned checks;
#define CHECK(v) do {++checks;if(!(v)){fprintf(stderr,"line %d: %s\n",__LINE__,#v);return 1;}}while(0)
'''
checks=r'''
static TbdUnitId fresh(void) {
 TbdUnitId d={0};d.kind=2;d.ncir_serial=0x0011223344556677ULL;
 d.ncir_ambient=15000;d.ncir_object=0x3ad2;return d;
}
static int readword(TbdUnitId *d,uint8_t command,uint8_t *bytes) {
 I2CSlave *s=(I2CSlave *)d;
 if(unit_event(s,I2C_START_SEND)||unit_send(s,command)||unit_event(s,I2C_START_RECV))return -1;
 for(int i=0;i<3;i++)bytes[i]=unit_recv(s);
 unit_event(s,I2C_NACK);unit_event(s,I2C_FINISH);return 0;
}
int main(void) {
 TbdUnitId d=fresh();I2CSlave *s=(I2CSlave *)&d;uint8_t b[3];
 // Independent published Melexis read-word vector: B4 07 B5 D2 3A ->30.
 CHECK(readword(&d,7,b)==0);CHECK(b[0]==0xd2&&b[1]==0x3a&&b[2]==0x30);
 CHECK(readword(&d,0x2e,b)==0);CHECK(b[0]==0x5a&&b[1]==0);
 uint64_t id=0;for(int cmd=0x3c;cmd<=0x3f;cmd++){
  CHECK(readword(&d,cmd,b)==0);id=(id<<16)|((uint16_t)b[1]<<8)|b[0];}
 CHECK(id==0x0011223344556677ULL);
 CHECK(unit_event(s,I2C_START_RECV)==-1); // No command / previous STOP.
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,7)==0);
 CHECK(unit_event(s,I2C_FINISH)==0);CHECK(unit_event(s,I2C_START_RECV)==-1);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0x2e)==0);
 CHECK(unit_send(s,0)==-1);CHECK(unit_event(s,I2C_START_RECV)==-1); // No EEPROM write.
 CHECK(readword(&d,0x2e,b)==0);CHECK(b[0]==0x5a);
 for(int cmd=0;cmd<256;cmd++){
  bool supported=cmd==6||cmd==7||cmd==0x2e||(cmd>=0x3c&&cmd<=0x3f);
  CHECK((readword(&d,cmd,b)==0)==supported);
 }
 d.measure_crc_error=true;CHECK(readword(&d,7,b)==0);CHECK(b[2]==0x31);
 CHECK(readword(&d,0x2e,b)==0);uint8_t good_id_pec=b[2];
 d.measure_crc_error=false;d.crc_error=true;
 CHECK(readword(&d,0x2e,b)==0);CHECK(b[2]==(good_id_pec^1));
 CHECK(readword(&d,7,b)==0);CHECK(b[2]==0x30);
 d.crc_error=false;d.ncir_sensor_error=true;
 CHECK(readword(&d,7,b)==0);CHECK(b[0]==0xd2&&b[1]==0xba);CHECK(b[2]!=0x30);
 d.ncir_sensor_error=false;CHECK(readword(&d,7,b)==0);CHECK(b[2]==0x30);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,7)==0);
 CHECK(unit_event(s,I2C_START_RECV)==0);unit_recv(s);unit_recv(s);unit_recv(s);
 CHECK(unit_recv(s)==0xff);CHECK(unit_event(s,I2C_START_RECV)==-1);
 unit_event(s,I2C_FINISH);CHECK(readword(&d,7,b)==0);CHECK(b[2]==0x30);
 // Existing MAX30100 identity and SGP30 feature/serial replies still work.
 memset(&d,0,sizeof(d));d.revision=5;unit_event(s,I2C_START_SEND);unit_send(s,0xff);
 unit_event(s,I2C_START_RECV);CHECK(unit_recv(s)==0x11);
 unit_event(s,I2C_START_SEND);unit_send(s,0xfe);unit_event(s,I2C_START_RECV);CHECK(unit_recv(s)==5);
 memset(&d,0,sizeof(d));d.kind=1;unit_event(s,I2C_START_SEND);unit_send(s,0x20);unit_send(s,0x2f);
 CHECK(unit_event(s,I2C_START_RECV)==-1);now=10;CHECK(unit_event(s,I2C_START_RECV)==0);
 CHECK(unit_recv(s)==0);CHECK(unit_recv(s)==0x22);CHECK(unit_recv(s)==0x65);
 // ToF4M identity uses a two-byte register pointer and rejects configuration writes.
 memset(&d,0,sizeof(d));d.kind=3;d.tof_model=0xeacc;
 CHECK(unit_event(s,I2C_START_RECV)==-1);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,1)==0);
 CHECK(unit_event(s,I2C_START_RECV)==-1); // partial pointer
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,1)==0);CHECK(unit_send(s,15)==0);
 CHECK(unit_event(s,I2C_START_RECV)==0);CHECK(unit_recv(s)==0xea);CHECK(unit_recv(s)==0xcc);CHECK(unit_recv(s)==0xff);
 CHECK(unit_event(s,I2C_FINISH)==0);CHECK(unit_event(s,I2C_START_RECV)==-1);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,1)==0);CHECK(unit_send(s,15)==0);CHECK(unit_send(s,0)==-1);
 CHECK(unit_event(s,I2C_START_RECV)==-1);CHECK(d.tof_model==0xeacc);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,1)==0);CHECK(unit_send(s,16)==0);
 CHECK(unit_event(s,I2C_START_RECV)==0);CHECK(unit_recv(s)==0xcc);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0)==0);
 CHECK(unit_event(s,I2C_START_RECV)==-1); // unsupported register is not ranging data
 d.tof_model=0;CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,1)==0);CHECK(unit_send(s,15)==0);
 CHECK(unit_event(s,I2C_START_RECV)==0);CHECK(unit_recv(s)==0);CHECK(unit_recv(s)==0);
 // Opt-in fixed-profile synthetic ranging; identity-only cases above retained.
 d=(TbdUnitId){0};d.kind=3;d.tof_ranging=true;d.tof_model=0xeacc;d.tof_distance=500;d.tof_status=9;s=(I2CSlave*)&d;
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x87)==0);CHECK(unit_send(s,0x40)==0);
 CHECK(d.initialized);CHECK(unit_event(s,I2C_START_RECV)==-1); // no read after configuration payload
 CHECK(unit_event(s,I2C_FINISH)==0);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x31)==0);CHECK(unit_event(s,I2C_START_RECV)==0);
 CHECK(unit_recv(s)==0);now+=100;
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x31)==0);CHECK(unit_event(s,I2C_START_RECV)==0);CHECK(unit_recv(s)==1);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x96)==0);CHECK(unit_event(s,I2C_START_RECV)==0);
 CHECK(unit_recv(s)==1);CHECK(unit_recv(s)==244);
 d.tof_status=4;CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x89)==0);CHECK(unit_event(s,I2C_START_RECV)==0);CHECK(unit_recv(s)==4);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x86)==0);CHECK(unit_send(s,1)==0);CHECK(d.ready_ms==now+120);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x89)==0);CHECK(unit_event(s,I2C_START_RECV)==0);CHECK(unit_recv(s)==255);
 now+=120;d.tof_never_ready=true;
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x31)==0);CHECK(unit_event(s,I2C_START_RECV)==0);CHECK(unit_recv(s)==0);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,0)==0);CHECK(unit_send(s,0x87)==0);CHECK(unit_send(s,0x80)==0);CHECK(!d.initialized);
 CHECK(unit_event(s,I2C_START_SEND)==0);CHECK(unit_send(s,1)==0);CHECK(unit_send(s,15)==0);CHECK(unit_send(s,0)==-1);CHECK(d.tof_model==0xeacc);
 printf("%u callback checks passed\n",checks);return 0;
}
'''
c=a.out/'model.c';c.write_text(shim+body+checks)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(c),'-o',str(a.out/'model')],check=True)
r=subprocess.run([str(a.out/'model')],capture_output=True,text=True)
(a.out/'result.json').write_text(json.dumps(dict(source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr,scope='actual model callbacks with host shim; no live QEMU or physical qualification'),indent=2)+'\n')
print(r.stdout,r.stderr,end='');raise SystemExit(r.returncode)
