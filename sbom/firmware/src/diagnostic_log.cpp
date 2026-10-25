#include "diagnostic_log.h"
#include "acquisition_record.h"
#include "diagnostic_log_json.h"
#include "record_frame.h"
#include "plc_core.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <cstdio>
#include <cstring>

#ifndef USE_ESP_IDF_LOG
#error "Diagnostic ownership requires the pinned Arduino IDF task-log route"
#endif
namespace {
diagnostics::Ring ring;
recording::Sender console_sender;
char console_wire[2304];
portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
struct Guard {Guard(){portENTER_CRITICAL(&mutex);}~Guard(){portEXIT_CRITICAL(&mutex);}};
// Exercise state belongs only to the loop task; no GPIO or sensor failure injection.
uint64_t exercise_id=0;
uint32_t exercise_remaining=0,exercise_generated=0;
uint64_t next_exercise_us=0;
int sdk_sink(const char* format,va_list args) {
    return diagnostic_log_vprintf(diagnostics::Origin::Sdk,format,args);
}
bool decimal(const char* p,uint64_t& value) {
    if(!p || !*p || (p[0]=='0' && p[1]))return false;
    value=0;
    for(;*p;++p) {
        if(*p<'0'||*p>'9')return false;
        uint64_t d=uint64_t(*p-'0');if(value>(UINT64_MAX-d)/10)return false;
        value=value*10+d;
    }
    return true;
}

}
bool diagnostic_log_console_busy(){return console_sender.active();}
void diagnostic_log_console_poll(){console_sender.poll(Serial,console_wire,millis());}
void diagnostic_log_begin() {esp_log_set_vprintf(sdk_sink);}
int diagnostic_log_vprintf(diagnostics::Origin origin,const char* format,va_list args) {
    // Per-call formatting is outside the retention lock and never writes USB.
    if(!format){Guard guard;ring.format_error();return -1;}
    char bytes[diagnostics::Entry::MaxBytes+1];
    int length=vsnprintf(bytes,sizeof(bytes),format,args);
    Guard guard;
    if(length<0){ring.format_error();return length;}
    const size_t retained=size_t(length)<diagnostics::Entry::MaxBytes?size_t(length):diagnostics::Entry::MaxBytes;
    return ring.append(origin,uint64_t(esp_timer_get_time()),reinterpret_cast<const uint8_t*>(bytes),retained,uint32_t(length)) ? int(retained) : 0;
}
int diagnostic_log_printf(diagnostics::Origin origin,const char* format,...) {
    va_list args;va_start(args,format);int length=diagnostic_log_vprintf(origin,format,args);va_end(args);return length;
}
diagnostics::Status diagnostic_log_status() {Guard guard;return ring.status();}
bool diagnostic_log_read(uint64_t sequence,diagnostics::Entry& entry) {Guard guard;return ring.read(sequence,entry);}
void diagnostic_log_poll() {
    if(!exercise_remaining)return;
    uint64_t now=uint64_t(esp_timer_get_time());if(now<next_exercise_us)return;
    --exercise_remaining;++exercise_generated;next_exercise_us=now+250000;
    // Actual Arduino macro route, visibly synthetic. No pin, source quality or
    // output changes occur. At most one bounded message per loop visit.
    log_e("TBD_DIAGNOSTIC_SELFTEST id=%llu sequence=%lu; no physical fault injected",
          (unsigned long long)exercise_id,(unsigned long)exercise_generated);
}
void diagnostic_log_console(const char* action,const char* value,bool extra) {
    if(console_sender.active())return; // dispatcher never overlaps commands
    StaticJsonDocument<2304> doc;
    doc["schema"]="tbd.diagnostics/v1";doc["logs"]="refused";
    if(!action || extra) {doc["reason"]="logs status | logs read <sequence> | logs exercise <1..32>";}
    else if(!strcmp(action,"status") && !value) {doc["logs"]="status";}
    else if(!strcmp(action,"read")) {
        uint64_t sequence=0;diagnostics::Entry entry;
        if(!decimal(value,sequence) || !sequence)doc["reason"]="invalid diagnostic sequence";
        else if(!diagnostic_log_read(sequence,entry)){doc["logs"]="missing";doc["requested_sequence"]=sequence;}
        else {
            doc["logs"]="entry";doc["sequence"]=entry.sequence;doc["enqueue_us"]=entry.enqueue_us;
            doc["origin"]=diagnostics::name(entry.origin);doc["retained_bytes"]=entry.retained_bytes;
            doc["formatted_bytes"]=entry.formatted_bytes;doc["truncated"]=entry.truncated;
            char hex[2*diagnostics::Entry::MaxBytes+1],preview[diagnostics::Entry::MaxBytes+1];
            diagnostics::hexadecimal(entry,hex);diagnostics::ascii_preview(entry,preview);
            doc["bytes_hex"]=hex;doc["ascii_preview"]=preview;
        }
    } else if(!strcmp(action,"exercise")) {
        uint64_t count=0;
        if(!decimal(value,count) || count<1 || count>32)doc["reason"]="exercise count must be 1..32";
        else if(plc_mode()!=PLC_PROGRAM)doc["reason"]="exercise requires PROGRAM";
        else if(exercise_remaining)doc["reason"]="exercise already active";
        else if(exercise_id==UINT64_MAX)doc["reason"]="exercise identity exhausted";
        else {++exercise_id;exercise_remaining=uint32_t(count);exercise_generated=0;next_exercise_us=0;doc["logs"]="exercise_scheduled";}
    } else doc["reason"]="invalid diagnostic command";
    // Same loop-owned boot identity as the acquisition records. Diagnostics
    // are raw retained evidence; retrieval does not authorize control/scoring.
    doc["boot_id"]=acquisition_record_boot_id();doc["clock"]="boot_monotonic_enqueue";
    doc["exercise_id"]=exercise_id;doc["exercise_remaining"]=exercise_remaining;doc["exercise_generated"]=exercise_generated;
    diagnostics::status_json(doc.createNestedObject("status"),diagnostic_log_status());
    size_t n=measureJson(doc);
    if(doc.overflowed() || n+2>sizeof(console_wire)) {
        const char* error="{\"logs\":\"refused\",\"reason\":\"diagnostic serialization capacity\"}\n";
        n=strlen(error);memcpy(console_wire,error,n);
    } else {
        n=serializeJson(doc,console_wire,sizeof(console_wire));console_wire[n++]='\n';
    }
    console_sender.start(n,millis());
}
