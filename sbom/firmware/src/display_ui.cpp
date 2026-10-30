// display_ui.cpp - on-device LCD operator panel for the TBD teaching PLC (Tbd).
// Boot logo ceremony, live HOME (mode hero + I/O at a glance), and 3-button navigation:
//   A = next/move   B = select/act   C = back
// Screens: HOME, MODE (RUN/PROGRAM/REMOTE), I/O browser (base board + expansion module),
// FORCES (active overrides + clear-all), INFO (identity + health).
// Renders on M5StamPLC.Display (M5GFX). First hardware increment of firmware/LCD_UX_SPEC.md.
#include <M5StamPLC.h>
#include "display_ui.h"
#include "plc_core.h"
#include "plc_point_map.h"
#include "m5stamplc_io.h"
#include "io_expansion.h"
#include "modbus_tcp_server.h"
#include "plc_auth.h"
#include "eth_net.h"
#include "loop_timing.h"
#include "device_acquisition.h"
#include <cstring>
#include <esp_timer.h>
#include <esp_system.h>
#include <cstdlib>
#include <new>

namespace {
struct PanelTiming {
  timing::PanelOperation operation; uint64_t start;
  explicit PanelTiming(timing::PanelOperation value):operation(value),start(esp_timer_get_time()){}
  ~PanelTiming(){plc_timing_panel_operation(operation,start,esp_timer_get_time());}
};
}


// Resolve the display at RUNTIME, never cache it in a static reference: a static ref
// initialized here can bind before the M5StamPLC library object is constructed
// (static-init-order fiasco) -> garbage panel -> null color-convert crash on first draw.
// Retained rendering is opt-in until exact-target memory/timing gates pass.
static M5Canvas* s_canvas=nullptr;
static display_capture::Snapshot* s_capture=nullptr;
static uint8_t* s_capture_bytes=nullptr;
static bool s_capture_pending=false;
static bool s_canvas_frozen=false;
static volatile bool s_injA=false,s_injB=false,s_injC=false; // UI-injected panel button presses
void display_ui_inject(char b){ if(b=='a'||b=='A')s_injA=true; else if(b=='b'||b=='B')s_injB=true; else if(b=='c'||b=='C')s_injC=true; }
// Maintainer physical-presence check: sample button A after the panel is up.
// Used by the secure build's boot recovery (hold A at reset to clear the password).
bool display_ui_btnA_held_at_boot(){
  M5StamPLC.update(); delay(25); M5StamPLC.update();
  return M5.BtnA.isPressed();
}
static display_capture::Ticket s_capture_ticket{};
static bool s_surface_attempted=false;
#ifdef ENABLE_DISPLAY_CAPTURE
static const char* s_capture_status="not_initialized";
#else
static const char* s_capture_status="disabled";
#endif
const char* display_ui_capture_status(){return s_capture_status;}
static uint64_t s_presented_us=0;
static bool draw_to_canvas(){return s_canvas&&(!s_canvas_frozen||s_capture_pending);}
static lgfx::LGFXBase& surface(){return draw_to_canvas()?static_cast<lgfx::LGFXBase&>(*s_canvas):static_cast<lgfx::LGFXBase&>(M5StamPLC.Display);}
#define D surface()
static void capture_surface_init(){
 if(s_surface_attempted)return;s_surface_attempted=true;
#ifdef ENABLE_DISPLAY_CAPTURE
 // No assumed PSRAM. Failure retains the original direct-to-panel renderer.
 if(M5StamPLC.Display.width()!=240||M5StamPLC.Display.height()!=135){s_capture_status="unsupported_geometry";return;}
 auto canvas=new(std::nothrow) M5Canvas(&M5StamPLC.Display);if(!canvas){s_capture_status="canvas_object_allocation_failed";return;}
 canvas->setPsram(false);canvas->setColorDepth(16);
 if(!canvas->createSprite(240,135)){s_capture_status="canvas_pixels_allocation_failed";delete canvas;return;}
 // The sprite becomes the frozen snapshot after submission. While frozen,
 // rendering uses the physical panel directly; the next capture fully redraws
 // the sprite before reuse. No second 64,800-byte allocation is required.
 if(canvas->bufferLength()!=display_capture::MaxBytes){s_capture_status="unsupported_canvas_stride";delete canvas;return;}
 auto bytes=static_cast<uint8_t*>(canvas->getBuffer());
 display_capture::Boot boot{};esp_fill_random(boot.data(),boot.size());
 auto capture=new(std::nothrow) display_capture::Snapshot(bytes,display_capture::MaxBytes,boot);
 if(!capture){s_capture_status="snapshot_object_allocation_failed";delete canvas;return;}
 canvas->setSwapBytes(true); // readRect(uint16_t*) returns native RGB565 in pinned M5GFX
 s_capture_bytes=bytes;s_capture=capture;s_canvas=canvas;s_capture_status="ready";
#endif
}
static void present_surface(){
 if(draw_to_canvas())s_canvas->pushSprite(0,0);
 s_presented_us=esp_timer_get_time();
 if(!s_capture_pending||!s_capture)return;
 // Normalize the completed software render into explicit RGB565 little-endian.
 // Hardware scan timing and this bounded conversion still need target qualification.
 uint16_t row[240];
 for(unsigned y=0;y<135;++y){
  s_canvas->readRect(0,y,240,1,row);
  for(unsigned x=0;x<240;++x){size_t n=(y*240+x)*2;s_capture_bytes[n]=row[x]&255;s_capture_bytes[n+1]=row[x]>>8;}
 }
 s_capture->publish(s_capture_ticket,esp_timer_get_time(),s_capture_bytes,display_capture::MaxBytes,240,135,0,s_presented_us);
 s_canvas_frozen=true; // normalized bytes are never drawn/pushed until full redraw
 s_capture_pending=false;
}
display_capture::Result display_ui_capture_request(display_capture::Ticket& ticket){
 if(!s_capture)return display_capture::Result::Unavailable;
 auto r=s_capture->request(esp_timer_get_time(),ticket);
 if(r==display_capture::Result::Ok){s_capture_ticket=ticket;s_capture_pending=true;}return r;
}
display_capture::Result display_ui_capture_manifest(const display_capture::Ticket& t,display_capture::Manifest& m){return s_capture?s_capture->manifest(t,esp_timer_get_time(),m):display_capture::Result::Unavailable;}
display_capture::Result display_ui_capture_read(const display_capture::Ticket& t,size_t offset,size_t length,uint8_t* out,size_t capacity){return s_capture?s_capture->read(t,esp_timer_get_time(),offset,length,out,capacity):display_capture::Result::Unavailable;}
display_capture::Result display_ui_capture_cancel(const display_capture::Ticket& t){
 if(!s_capture)return display_capture::Result::Unavailable;
 auto r=s_capture->cancel(t,esp_timer_get_time());if(r==display_capture::Result::Ok||r==display_capture::Result::Expired)s_capture_pending=false;return r;
}

// ---- Tbd palette (RGB565) ----
static uint16_t BG,SURF,LINE,UIWHITE,MUTED,ACC,OKC,WARNC,FAULTC,VIO;
static bool s_init=false;
static uint32_t s_bootT=0; static bool s_bootDrawn=false, s_built=false;
// color565 computed directly, so palette setup never touches the (maybe-not-ready) panel
#define RGB565(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))

enum Scr { S_BOOT, S_HOME, S_MODE, S_IO, S_GROVE, S_FORCES, S_INFO, S_LOG };
static Scr s_scr=S_BOOT;
static int s_menu=0;                 // HOME menu cursor: 0 MODE 1 I/O 2 GROVE 3 FORCE 4 INFO
static int s_target=PLC_RUN;         // MODE target
static int s_iocur=0;                // I/O list cursor
static int s_grovecur=0;             // GROVE (Grove/expansion sensor) list cursor
static uint32_t s_lastBtn=0, s_lastDraw=0, s_sig=0;
static int s_ledMode=-1;
static PlcPanelInputs s_inputs;
static uint32_t s_lastInputStamp=0;
static bool scan_di(int i){return s_inputs.available && ((s_inputs.values >> i)&1);}
static bool scan_di_forced(int i){return s_inputs.available && ((s_inputs.force_mask >> i)&1);}


// ---- element model: base board + expansion module, flat indexed list ----
enum EKind { E_DI, E_RLY, E_MECH, E_SSR, E_AIN };
struct Elem { char id[8]; char board[10]; EKind kind; int idx; };
static Elem s_el[24]; static int s_nel=0;
static void build_elems(){
  s_nel=0;
  for(int i=0;i<8;i++){ Elem&e=s_el[s_nel++]; snprintf(e.id,8,"DI%d",i); strcpy(e.board,"BASE"); e.kind=E_DI; e.idx=i; }
  for(int i=0;i<4;i++){ Elem&e=s_el[s_nel++]; snprintf(e.id,8,"RLY%d",i); strcpy(e.board,"BASE"); e.kind=E_RLY; e.idx=i; }
  if(ioexp_present()){
    { Elem&e=s_el[s_nel++]; strcpy(e.id,"MECH"); strcpy(e.board,"IO-EXP"); e.kind=E_MECH; e.idx=0; }
    for(int i=0;i<2;i++){ Elem&e=s_el[s_nel++]; snprintf(e.id,8,"SSR%d",i); strcpy(e.board,"IO-EXP"); e.kind=E_SSR; e.idx=i; }
    for(int i=0;i<2;i++){ Elem&e=s_el[s_nel++]; snprintf(e.id,8,"AIN%d",i); strcpy(e.board,"IO-EXP"); e.kind=E_AIN; e.idx=i; }
  }
}
static bool elem_state(const Elem&e){
  switch(e.kind){
    case E_DI:  return scan_di(e.idx);
    case E_RLY: return plc_coil_live(1 + e.idx);
    case E_SSR: return plc_coil_live(5 + e.idx);
    case E_MECH:return plc_coil_live(0);
    default:    return false;
  }
}
static bool elem_forced(const Elem&e){
  if(e.kind==E_RLY) return plc_coil_is_forced(1 + e.idx);
  if(e.kind==E_SSR) return plc_coil_is_forced(5 + e.idx);
  if(e.kind==E_MECH) return plc_coil_is_forced(0);
  if(e.kind==E_DI)  return scan_di_forced(e.idx);
  return false;
}

// ---- helpers ----
static void tL(const char*s,int x,int y,float sz,uint16_t c){ D.setTextColor(c); D.setTextSize(sz); D.drawString(s,x,y); }
static void tC(const char*s,int x,int y,float sz,uint16_t c){ D.setTextSize(sz); int w=D.textWidth(s); D.setTextColor(c); D.drawString(s,x-w/2,y); }
static uint16_t modeColor(int m){ return m==PLC_RUN?OKC : m==2?WARNC : FAULTC; }
static const char* modeShort(int m){ return m==PLC_RUN?"RUN" : m==2?"REM":"PROG"; }
static const char* modeLong(int m){ return m==PLC_RUN?"RUN" : m==2?"REMOTE":"PROGRAM"; }

static void colors(){
  BG=RGB565(0x0E,0x11,0x16); SURF=RGB565(0x1B,0x20,0x27); LINE=RGB565(0x2b,0x33,0x3d);
  UIWHITE=RGB565(0xEA,0xEC,0xEE); MUTED=RGB565(0x7A,0x82,0x8C); ACC=RGB565(0x3B,0x6D,0xF5);
  OKC=RGB565(0x39,0xD3,0x53); WARNC=RGB565(0xF5,0xA5,0x24); FAULTC=RGB565(0xF0,0x46,0x3C);
  VIO=RGB565(0xB3,0x6B,0xFF);
}

// Small provenance strip updates separately; never redraw the whole panel per scan.
static void input_stamp(int y){
  char text[40],age[12]; uint16_t color=MUTED;
  if(!s_inputs.available){snprintf(text,sizeof(text),"DI: no completed scan");color=WARNC;}
  else {
    uint64_t now=esp_timer_get_time();
    if(now<s_inputs.completed_us){snprintf(age,sizeof(age),"clock?");color=WARNC;}
    else {uint64_t elapsed=(now-s_inputs.completed_us)/1000;
      if(elapsed>99999)snprintf(age,sizeof(age),">99s");else snprintf(age,sizeof(age),"%llums",(unsigned long long)elapsed);
      if(elapsed>500)color=WARNC;
    }
    if(s_inputs.generated)snprintf(text,sizeof(text),"DI e%llu #%llu %s",(unsigned long long)s_inputs.epoch,(unsigned long long)s_inputs.sequence,age);
    else snprintf(text,sizeof(text),"DI scan #%llu %s",(unsigned long long)s_inputs.sequence,age);
  }
  D.fillRect(4,y,232,9,BG);tL(text,6,y,1,color);
}

// ---- chin (persistent bottom strip with A/B/C hints) ----
static void chin(const char*a,const char*b,const char*c){
  D.fillRect(0,121,240,14,SURF);
  int m=plc_mode(); D.fillCircle(7,128,3,modeColor(m)); tL(modeShort(m),13,124,1,UIWHITE);
  char v[24]; snprintf(v,24,"%.1fV",io.voltage_read()); tL(v,52,124,1,MUTED);
  int fc=plc_force_count(); if(fc>0){ char f[12]; snprintf(f,12,"%dF",fc); tL(f,92,124,1,VIO); }
  char hint[40]; snprintf(hint,40,"A:%s B:%s C:%s",a,b,c);
  D.setTextSize(1); int hw=D.textWidth(hint); tL(hint,236-hw,124,1,ACC);
}

// ---- boot ----
void display_ui_boot(){
  // Set state ONLY here. ALL drawing and any panel/I2C access happens in display_ui_tick(),
  // which runs from loop() where the display panel is guaranteed initialized. Drawing during
  // setup() crashed (panel not ready yet -> fillScreen null color-convert).
  colors(); s_init=true; s_scr=S_BOOT; s_bootDrawn=false; s_built=false;
}
static void renderBoot(){
  D.fillScreen(BG);
  int cx=D.width()/2;
  D.drawCircle(cx,40,19,ACC); D.drawEllipse(cx,40,7,19,ACC); D.drawEllipse(cx,40,19,7,ACC);
  D.setTextSize(3);
  int wAll=D.textWidth("TBD"), x0=cx-wAll/2;
  D.setTextColor(UIWHITE); D.drawString("T",x0,66);
  D.setTextColor(ACC);   D.drawString("BD",x0+D.textWidth("T"),66);
  tC("TBD teaching PLC",cx,96,1,MUTED);
  tC("secure by design",cx,108,1,MUTED);
}

// ---- renders ----
static void renderHome(){
  D.fillScreen(BG);
  tL("M5 StamPLC",6,4,1,MUTED);
  { String ips=eth_ok()?eth_ip():String("no-net"); D.setTextSize(1); int w=D.textWidth(ips.c_str()); tL(ips.c_str(),234-w,4,1,MUTED); }
  int m=plc_mode();
  D.drawRoundRect(4,20,104,52,5,LINE);
  tL(modeShort(m),12,28,4,modeColor(m));
  tL("mode",14,60,1,MUTED);
  // inputs
  tL("SCAN DI",120,20,1,MUTED);
  for(int i=0;i<8;i++){ int x=120+i*15,y=32; bool on=scan_di(i);
    D.fillRoundRect(x,y,11,11,2,on?OKC:SURF); if(!s_inputs.available)tL("?",x+2,y+1,1,MUTED); if(scan_di_forced(i)) D.drawRoundRect(x,y,11,11,2,VIO); }
  // outputs
  tL("DRV CMD",120,50,1,MUTED);
  for(int i=0;i<4;i++){ int x=120+i*15,y=62; bool on=plc_relay_commanded(i);
    D.fillRoundRect(x,y,11,11,2,on?OKC:SURF); if(plc_coil_is_forced(1 + i)) D.drawRoundRect(x,y,11,11,2,VIO); }
  if(ioexp_present()){ char mm[20]; snprintf(mm,20,"+IO-EXP @0x%02X",ioexp_addr()); tL(mm,120,80,1,ACC); }
  int fc=plc_force_count(); if(fc>0){ char f[18]; snprintf(f,18,"%d FORCED",fc); tL(f,8,80,1,VIO); }
  input_stamp(88);
  // menu
  const char* items[5]={"MODE","I/O","GROVE","FORCE","INFO"};
  int x=6; for(int i=0;i<5;i++){ bool on=i==s_menu; int w=D.textWidth(items[i])+10;
    if(on) D.fillRoundRect(x,98,w,16,4,ACC); tL(items[i],x+5,101,1,on?BG:MUTED); x+=w+6; }
  chin("next","open","-");
}
static void renderMode(){
  D.fillScreen(BG);
  tL("PROCESSOR MODE",6,6,2,UIWHITE);
  int cur=plc_mode();
  for(int i=0;i<3;i++){ int y=36+i*24; bool sel=i==s_target;
    if(sel) D.drawRoundRect(4,y-3,150,22,4,ACC);
    char row[16]; snprintf(row,16,"%s %s",i==cur?"\x07":" ",modeLong(i));
    tL(row,12,y,2,sel?modeColor(i):MUTED); }
  if(s_target==2) tL("network gets authority",6,108,1,WARNC);
  chin("next","APPLY","back");
}
static void renderIO(){
  D.fillScreen(BG);
  tL("I/O: scan DI; output requests",6,6,1,MUTED);
  int top=s_iocur-3; if(top<0)top=0; if(top>s_nel-6)top=s_nel-6; if(top<0)top=0;
  for(int r=0;r<6 && top+r<s_nel;r++){ int i=top+r; const Elem&e=s_el[i]; int y=22+r*15;
    bool sel=i==s_iocur; if(sel) D.fillRoundRect(2,y-1,236,15,3,SURF);
    char lbl[24]; snprintf(lbl,24,"%s %s",e.board,e.id);
    tL(lbl,8,y,1,sel?UIWHITE:MUTED);
    char st[16];
    if(e.kind==E_AIN){ int16_t v1=0,v2=0; int32_t i1=0,i2=0; bool valid=ioexp_read(&v1,&i1,&v2,&i2);
      int mv=e.idx==0?v1:v2; if(valid)snprintf(st,16,"%d mV",mv);else snprintf(st,16,"unavailable"); tL(st,150,y,1,UIWHITE); }
    else { bool on=elem_state(e); tL(e.kind==E_DI&&!s_inputs.available?"unknown":on?"ON":"off",150,y,1,on?OKC:MUTED);
      if(elem_forced(e)) tL("[F]",180,y,1,VIO); }
    if(sel){ tL("\x10",226,y,1,ACC); }
  }
  input_stamp(111);
  chin("next","act","back");
}
static void renderForces(){
  D.fillScreen(BG);
  tL("ACTIVE FORCES",6,6,2,VIO);
  int y=32,n=0;
  const char* labels[7]={"IO MECH","BASE RLY0","BASE RLY1","BASE RLY2","BASE RLY3","IO SSR0","IO SSR1"};
  auto row=[&](const char* label,bool on){
    if(n<5){char text[30];snprintf(text,sizeof(text),"%s = %s",label,on?"ON":"OFF");tL(text,10,y,1,VIO);y+=13;}
    n++;
  };
  for(int i=0;i<7;i++) if(plc_coil_is_forced(i)) row(labels[i],plc_coil_live(i));
  if(plc_coil_is_forced(pointmap::AUX_COIL0)) row("AUX COIL0",plc_coil_live(pointmap::AUX_COIL0));
  if(plc_coil_is_forced(pointmap::AUX_COIL1)) row("AUX COIL1",plc_coil_live(pointmap::AUX_COIL1));
  for(int i=0;i<8;i++) if(plc_di_is_forced(i)){char label[16];snprintf(label,sizeof(label),"BASE DI%d",i);row(label,plc_di_live(i));}
  if(n>5){char more[24];snprintf(more,sizeof(more),"+%d more (console forces)",n-5);tL(more,10,y,1,MUTED);}
  if(!n) tL("none",10,32,1,MUTED);
  chin("-","CLEAR ALL","back");
}
static void renderInfo(){
  D.fillScreen(BG);
  tL("IDENTITY / HEALTH",6,6,2,UIWHITE);
  char l[40]; int y=32;
  snprintf(l,40,"MAC  %s",plc_device_mac()); tL(l,8,y,1,MUTED); y+=13;
  snprintf(l,40,"IP   %s",eth_ok()?eth_ip().c_str():"no-net"); tL(l,8,y,1,MUTED); y+=13;
  snprintf(l,40,"mode %s  scan %luus",modeLong(plc_mode()),(unsigned long)plc_scan_last_us()); tL(l,8,y,1,MUTED); y+=13;
  snprintf(l,40,"temp %.1fC  in %.2fV %.2fA",io.temp_read(),io.voltage_read(),io.current_read()); tL(l,8,y,1,MUTED); y+=13;
  if(ioexp_present()){ snprintf(l,40,"IO-EXP @0x%02X fw0x%02X",ioexp_addr(),ioexp_fw()); tL(l,8,y,1,ACC); }
  else tL("no expansion module",8,y,1,MUTED);
  chin("-","LOG","back");
}

// ---- EVENT LOG: source-attributed write history from the plc_log ring ----
static void renderLog(){
  D.fillScreen(BG);
  tL("EVENT LOG",6,6,2,UIWHITE);
  tL("write provenance - newest first",6,24,1,MUTED);
  int n=plc_log_count(); char buf[112]; int y=36, shown=0;
  for(int i=0;i<n && shown<6;i++){
    if(!plc_log_get(i,buf,sizeof(buf))) continue;
    uint16_t col = (strstr(buf," MODBUS")||strstr(buf," DNP3")||strstr(buf," IEC104")||strstr(buf," S7")||strstr(buf," OPCUA")||strstr(buf," ENIP")||strstr(buf," BACNET")) ? FAULTC
                 : strstr(buf,"FORCE") ? VIO : MUTED;
    buf[38]=0;                 // clip to fit 240px at text size 1
    tL(buf,6,y,1,col); y+=13; shown++;
  }
  if(n==0) tL("no events recorded",6,36,1,MUTED);
  chin("-","-","back");
}

// ---- GROVE: Grove / expansion sensors on the PORT.A sensor bus (Wire1) ----
// The on-panel analog of the `sensors` console command: these modules live on the
// sensor plane (PORT.A / Wire1), NOT the relay/DI control plane the I/O screen shows,
// which is why a plugged-in ENV/NCIR/ToF never appeared under I/O. Read-only glance;
// A scrolls, C goes back. Data read from the acquisition engine on the loop task.
static void renderGrove(){
  D.fillScreen(BG);
  tL("GROVE SENSORS",6,6,2,UIWHITE);
  auto& e=device_acquisition_state(); uint32_t now=millis();
  int total=(int)e.count;
  char sub[48]; snprintf(sub,sizeof(sub),"PORT.A bus  %d found  sampling %s",total,e.enabled?"on":"off");
  tL(sub,6,26,1,e.enabled?MUTED:WARNC);
  if(total==0){
    tL("No Grove module detected.",8,46,1,MUTED);
    tL("Plug into PORT.A (red port),",8,62,1,MUTED);
    tL("then console:  devices scan",8,76,1,ACC);
    chin("-","-","back"); return;
  }
  if(s_grovecur>=total) s_grovecur=0;
  int topv=s_grovecur-1; if(topv>total-3)topv=total-3; if(topv<0)topv=0;
  int y=40;
  for(int r=0;r<3 && topv+r<total;r++){ int i=topv+r; const auto& s=e.slots[i]; const auto& d=s.device; const auto& v=s.value;
    acquisition::Quality q=s.current(now,e.enabled);
    bool sel=i==s_grovecur;
    uint16_t qc = q==acquisition::Quality::Valid?OKC
                : (q==acquisition::Quality::Warmup||q==acquisition::Quality::Stale||q==acquisition::Quality::Paused||q==acquisition::Quality::Awaiting)?WARNC
                : FAULTC;
    if(sel) D.fillRoundRect(2,y-1,236,25,3,SURF);
    char name[20]; snprintf(name,sizeof(name),"%s",d.driver);
    char* dash=strstr(name,"-candidate"); if(dash)*dash=0;
    char hdr[34]; snprintf(hdr,sizeof(hdr),"%s 0x%02X %s",d.bus?"IN":"PA",d.address,name);
    tL(hdr,8,y,1,sel?UIWHITE:MUTED);
    const char* qn=acquisition::name(q); D.setTextSize(1); int qw=D.textWidth(qn); tL(qn,232-qw,y,1,qc);
    char rd[64];
    if(!s.has_sample) snprintf(rd,sizeof(rd),"(awaiting first sample)");
    else switch(s.kind){
      case acquisition::Kind::Env:  snprintf(rd,sizeof(rd),"%.1fC  %.0f%%RH  %.0fhPa",
                                      double(v.temp_c),double(v.rh_pct),double(v.pressure_pa)/100.0); break;
      case acquisition::Kind::Ncir: snprintf(rd,sizeof(rd),"obj %.1fC  amb %.1fC",
                                      double(v.object_centi_c)/100.0,double(v.ambient_centi_c)/100.0); break;
      case acquisition::Kind::Tof:  snprintf(rd,sizeof(rd),"dist %ld mm",(long)v.distance_mm); break;
      case acquisition::Kind::Sgp:  snprintf(rd,sizeof(rd),"eCO2 %u  TVOC %u",v.eco2_ppm,v.tvoc_ppb); break;
      case acquisition::Kind::Heart:snprintf(rd,sizeof(rd),"IR %u  RED %u",v.ir,v.red); break;
      default:                      snprintf(rd,sizeof(rd),"ch1 %dmV  ch2 %dmV",v.mv1,v.mv2); break;
    }
    tL(rd,14,y+12,1,s.has_sample?UIWHITE:MUTED);
    y+=26;
  }
  chin(total>3?"next":"-","-","back");
}

// ---- input ----
static void act_io(){
  if(s_iocur<0||s_iocur>=s_nel) return; const Elem&e=s_el[s_iocur];
  if(e.kind==E_RLY || e.kind==E_SSR || e.kind==E_MECH){
    int a=e.kind==E_RLY ? 1 + e.idx : e.kind==E_SSR ? 5 + e.idx : 0;
    if(plc_coil_is_forced(a)) plc_unforce_coil(a,SRC_SYSTEM,"panel");
    else plc_force_coil(a,!plc_coil_live(a),SRC_SYSTEM,"panel");
    M5StamPLC.tone(700,80);
  } else if(e.kind==E_DI){ int a=e.idx;
    if(plc_di_is_forced(a)) plc_unforce_di(a,SRC_SYSTEM,"panel");
    else plc_force_di(a,!plc_di_live(a),SRC_SYSTEM,"panel");
    M5StamPLC.tone(700,80);
  }
}
static void handle(){
  // Buttons: read the debounced level via M5.BtnA (the M5Unified global folds to
  // a fixed address - reading M5StamPLC.BtnA instead loaded a corruptible pointer
  // and faulted). Derive the rising edge OURSELVES: multiple subsystems on both
  // cores call M5.update(), so M5Unified's own wasPressed() edge is frequently
  // consumed by a different update() before we get here. A local edge off the
  // stable isPressed() level is correct regardless of how often/where update runs.
  static bool s_pA=false, s_pB=false, s_pC=false;
  bool cA=M5.BtnA.isPressed(), cB=M5.BtnB.isPressed(), cC=M5.BtnC.isPressed();
  bool a = (cA && !s_pA)||s_injA, b = (cB && !s_pB)||s_injB, c = (cC && !s_pC)||s_injC;
  s_pA=cA; s_pB=cB; s_pC=cC; s_injA=s_injB=s_injC=false;
  if(!a&&!b&&!c) return;
  switch(s_scr){
    case S_HOME:
      if(a) s_menu=(s_menu+1)%5;
      else if(b){ if(s_menu==0){s_scr=S_MODE;s_target=plc_mode();} else if(s_menu==1){s_scr=S_IO;s_iocur=0;} else if(s_menu==2){s_scr=S_GROVE;s_grovecur=0;} else if(s_menu==3)s_scr=S_FORCES; else s_scr=S_INFO; }
      break;
    case S_MODE:
      if(a) s_target=(s_target+1)%3;
      else if(b){ plc_set_mode((PlcMode)s_target,SRC_SYSTEM,"panel"); M5StamPLC.tone(1200,90); s_scr=S_HOME; }
      else if(c) s_scr=S_HOME;
      break;
    case S_IO:
      if(a) s_iocur=(s_iocur+1)%s_nel;
      else if(b) act_io();
      else if(c) s_scr=S_HOME;
      break;
    case S_GROVE:{
      int n=(int)device_acquisition_state().count;
      if(a){ if(n>0) s_grovecur=(s_grovecur+1)%n; }
      else if(c) s_scr=S_HOME;
      break; }
    case S_FORCES:
      if(b){ plc_clear_all_forces(SRC_SYSTEM,"panel"); M5StamPLC.tone(880,120); }
      else if(c) s_scr=S_HOME;
      break;
    case S_INFO: if(b) s_scr=S_LOG; else if(c) s_scr=S_HOME; break;
    case S_LOG: if(c) s_scr=S_INFO; break;
    default: break;
  }
  s_sig=0; // force redraw after any input
}

// Content signature of the GROVE screen, quantized to what it actually displays, so
// the screen redraws only when a shown value changes — no periodic full-clear flicker
// (same change-detection the HOME screen uses). Stable readings hash the same → no redraw.
static uint32_t grove_sig(){
  auto& e=device_acquisition_state(); uint32_t now=millis();
  uint32_t s=2166136261u ^ (uint32_t)e.count ^ (e.enabled?0x55u:0u);
  for(size_t i=0;i<e.count;++i){ const auto& sl=e.slots[i]; const auto& v=sl.value;
    s=s*131u + sl.device.address + (uint32_t)sl.current(now,e.enabled)*7u + (sl.has_sample?3u:0u);
    s=s*131u + (uint32_t)(int32_t)(v.temp_c*10.0f) + (uint32_t)v.rh_pct
            + (uint32_t)(v.pressure_pa/100.0f) + (uint32_t)(v.gas_ohm/1000.0f);
    s=s*131u + (uint32_t)v.object_centi_c + (uint32_t)v.ambient_centi_c + (uint32_t)(v.distance_mm+1)
            + v.eco2_ppm + v.tvoc_ppb + v.ir + (uint32_t)v.mv1 + (uint32_t)v.mv2;
  }
  return s;
}
static uint32_t sig(){
  uint32_t s=(uint32_t)s_scr*131 + s_menu*17 + s_iocur*7 + s_grovecur*11 + plc_mode()*3 + s_target;
  s=s*257+s_inputs.available;
  s=s*257+s_inputs.values; s=s*257+s_inputs.force_mask;
  for(int i=0;i<4;i++) s=s*2+ (plc_relay_commanded(i)?1:0);
  s+=plc_force_count()*1009;
  if(s_scr==S_GROVE) s=s*1000003u + grove_sig();   // redraw GROVE on reading change, not on a timer
  return s;
}

void display_ui_tick(){
  if(!s_init) return;
  capture_surface_init();
  uint32_t now=millis();
  if(!s_built){ build_elems(); s_built=true; }   // safe here: ioexp_begin() has already run
  if(now-s_lastBtn>=35){ s_lastBtn=now;
    static uint32_t s_busB=0; if(g_bustrace && now-s_busB>=250){ s_busB=now; plc_bus_log(0x43,'R',"poll buttons (PI4IOE)",0,false); }
    { PanelTiming measurement(timing::BUTTON_UPDATE); M5StamPLC.update(); }
    if(s_scr!=S_BOOT) handle(); }
  if(s_scr==S_BOOT){
    if(!s_bootDrawn){ PanelTiming measurement(timing::BOOT_RENDER); renderBoot(); present_surface(); M5StamPLC.setStatusLight(0x10,0x28,0xF0); M5StamPLC.tone(1500,90); s_bootDrawn=true; s_bootT=now; }
    else if(now-s_bootT>1600){ s_scr=S_HOME; s_sig=0; s_lastDraw=0; }
    return;
  }
  plc_panel_inputs_capture(s_inputs); // one coherent copy; no I2C in change detection
  int m=plc_mode();
  if(m!=s_ledMode){ s_ledMode=m; if(m==PLC_RUN)M5StamPLC.setStatusLight(0,0x30,0x08); else if(m==2)M5StamPLC.setStatusLight(0x30,0x1c,0); else M5StamPLC.setStatusLight(0x30,0,0); }
  uint32_t g=sig();
  if(g!=s_sig || s_capture_pending){ s_sig=g; s_lastDraw=now;   // redraw only when content changes -> no periodic flicker
    PanelTiming measurement(timing::NORMAL_RENDER);
    switch(s_scr){ case S_MODE:renderMode();break; case S_IO:renderIO();break;
      case S_GROVE:renderGrove();break;
      case S_FORCES:renderForces();break; case S_INFO:renderInfo();break; case S_LOG:renderLog();break; default:renderHome(); }
    present_surface();
    s_lastInputStamp=now;
  } else if(now-s_lastInputStamp>=250 && (s_scr==S_HOME||s_scr==S_IO)) {
    // Live-refresh only the small provenance strip (HOME/IO). The GROVE screen is
    // driven by grove_sig() in the change-detection path above, so it redraws when a
    // reading changes rather than on a 250 ms timer — which was the flicker source.
    s_lastInputStamp=now;
    input_stamp(s_scr==S_HOME?88:111);
    present_surface();
  }
}
