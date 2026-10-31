#include "network_access.h"
#include "mqtt_transport.h"
#ifdef ENABLE_ETHERNET
#include "net_types.h"
#include "eth_net.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include <atomic>
#include <string.h>
// ESP-MQTT runs in its own task; the W5500 belongs to the comms task. Two paths join them:
//  * control - connect, write and close go through a one-slot request queue that the comms
//    task answers on its next pass (each is one bounded hardware operation);
//  * receive - on every pass the comms task PUMPS what the socket holds into a stream buffer,
//    and the MQTT task reads that buffer directly, so a read never waits for a comms pass.
// (When every read was a round trip, ESP-MQTT's byte-at-a-time header reads cost about four
// comms passes per message. In the twin that capped delivery near two messages a second, so a
// phone page's steady stream queued for seconds and the 8 s freshness bits lied. Earlier still,
// a request reached after its short poll deadline was answered "no data" even with bytes
// waiting, and the MQTT task never read at all.)
namespace {
constexpr size_t CAPACITY=1536;     // one write request
constexpr size_t RX_BYTES=4096;     // received bytes waiting for the MQTT task (2 W5500 socket buffers)
constexpr size_t PUMP_CHUNK=512;    // most bytes moved per comms pass (bounds the SPI time spent here)
enum Operation { Connect, Write, PollWrite, Close };
struct Request { uint32_t id;Operation op;int count,port;int64_t deadline;char host[64];uint8_t bytes[CAPACITY]; };
struct Response { uint32_t id;int result; };
QueueHandle_t requests=nullptr,responses=nullptr;
SemaphoreHandle_t caller=nullptr;
StreamBufferHandle_t received=nullptr;  // comms task writes, the MQTT task reads
NetClient net;
std::atomic<bool> initialized{false};
std::atomic<bool> linked{false};        // comms side: the socket is up (cleared on close / peer loss)
uint32_t serial=0;
int held=-1;                            // MQTT task only: the byte poll_read took to see data waiting
int64_t clock_ms(){return esp_timer_get_time()/1000;}
TickType_t wait_ticks(int ms){if(ms<=0)return 0;if(ms>5000)ms=5000;return pdMS_TO_TICKS(ms);}
int exchange(Operation op,const char* input,int n,int timeout,const char* host=nullptr,int port=0){
    if(!initialized.load()||n<0||timeout<0)return -1;
    // Bounded transport contract; no task pointer is stored beyond this call.
    if(timeout>5000)timeout=5000;
    if(xSemaphoreTake(caller,pdMS_TO_TICKS(5000))!=pdTRUE)return -1;
    Request q{};q.id=++serial;q.op=op;q.count=n>int(CAPACITY)?int(CAPACITY):n;q.port=port;
    q.deadline=clock_ms()+(timeout>0?timeout:1);
    if(host){if(strlen(host)>=sizeof(q.host)){xSemaphoreGive(caller);return -1;}strcpy(q.host,host);}
    if(input&&q.count)memcpy(q.bytes,input,q.count);
    int result=-1;
    if(xQueueSend(requests,&q,pdMS_TO_TICKS(20))==pdTRUE){
        // Allow the owner to complete a bounded hardware connect/write that
        // began before its deadline. A timeout remains an ambiguous I/O result.
        const int64_t deadline=q.deadline+1200;
        Response r;
        while(clock_ms()<deadline){
            if(xQueueReceive(responses,&r,pdMS_TO_TICKS(10))==pdTRUE&&r.id==q.id){result=r.result;break;}
        }
    }
    xSemaphoreGive(caller);return result;
}
// Receive side (MQTT task). 0 = nothing within the timeout, -1 = the connection is gone and
// every byte it delivered has been read (ESP-MQTT then reconnects).
int read_cb(esp_transport_handle_t,char* b,int n,int timeout){
    if(!initialized.load()||n<0)return -1;
    if(n==0)return 0;
    int got=0;
    if(held>=0){b[0]=char(held);held=-1;got=1;}
    if(got<n){
        if(!got&&!linked.load()&&xStreamBufferIsEmpty(received))return -1;
        got+=int(xStreamBufferReceive(received,b+got,size_t(n-got),got?0:wait_ticks(timeout)));
    }
    if(got)return got;
    return linked.load()?0:-1;
}
int poll_read_cb(esp_transport_handle_t,int timeout){
    if(!initialized.load())return -1;
    if(held>=0||!xStreamBufferIsEmpty(received))return 1;
    if(!linked.load())return -1;
    uint8_t c;
    if(xStreamBufferReceive(received,&c,1,wait_ticks(timeout))==1){held=c;return 1;}
    return linked.load()?0:-1;
}
int connect_cb(esp_transport_handle_t,const char* host,int port,int timeout){held=-1;return exchange(Connect,nullptr,0,timeout,host,port);}
int write_cb(esp_transport_handle_t,const char* b,int n,int timeout){return exchange(Write,b,n,timeout);}
int poll_write_cb(esp_transport_handle_t,int timeout){return exchange(PollWrite,nullptr,0,timeout);}
int close_cb(esp_transport_handle_t){held=-1;return exchange(Close,nullptr,0,1000);}
int destroy_cb(esp_transport_handle_t){return close_cb(nullptr);}
// Comms side. The MQTT socket has its own keepalive, so the idle-socket reaper leaves it alone
// while it is ours (it read as "idle" because this pump empties it within a pass).
void drop(){net.stop();linked.store(false);eth_reap_exempt(-1);}
void pump(){
    if(!linked.load())return;
    static uint8_t chunk[PUMP_CHUNK];
    const int available=net.available();
    if(available>0){
        size_t n=size_t(available),room=xStreamBufferSpacesAvailable(received);
        if(n>room)n=room;                  // full: leave the rest in the W5500 (TCP flow control)
        if(n>sizeof(chunk))n=sizeof(chunk);
        if(n){const int got=net.read(chunk,n);if(got>0)xStreamBufferSend(received,chunk,size_t(got),0);}
        return;
    }
    if(!net.connected()){linked.store(false);eth_reap_exempt(-1);}   // peer gone, every byte handed over
}
}
esp_transport_handle_t mqtt_w5500_transport(){
    if(!initialized.load()){
        requests=xQueueCreate(1,sizeof(Request));responses=xQueueCreate(1,sizeof(Response));caller=xSemaphoreCreateMutex();
        received=xStreamBufferCreate(RX_BYTES,1);
        if(!requests||!responses||!caller||!received){
            if(requests)vQueueDelete(requests);if(responses)vQueueDelete(responses);if(caller)vSemaphoreDelete(caller);
            if(received)vStreamBufferDelete(received);
            requests=nullptr;responses=nullptr;caller=nullptr;received=nullptr;return nullptr;
        }
        initialized.store(true);
    }
    esp_transport_handle_t t=esp_transport_init();if(!t)return nullptr;
    esp_transport_set_func(t,connect_cb,read_cb,write_cb,close_cb,poll_read_cb,poll_write_cb,destroy_cb);
    esp_transport_set_default_port(t,1883);return t;
}
void mqtt_w5500_service(){
    if(!initialized.load())return;
    // Close the W5500 connection even with no pending request. Already delivered
    // broker/retained data cannot be revoked by a later controller policy.
    if(plc_project_host_acl_enabled()){net.stop();linked.store(false);eth_reap_exempt(-1);}
    pump();
    static Request q;static bool pending=false;
    if(!pending){if(xQueueReceive(requests,&q,0)!=pdTRUE)return;pending=true;}
    Response r{};r.id=q.id;r.result=-1;
    // `due`: the caller's timeout has elapsed, so a wait (not writable) may now answer 0.
    // `stale`: the caller has given up (it waits deadline+1200 ms), so the request must not run.
    const int64_t now=clock_ms();
    const bool due=now>=q.deadline, stale=now>=q.deadline+1000;
    if(q.op==Close){drop();xStreamBufferReset(received);r.result=0;}
    else if(plc_project_host_acl_enabled()){drop();r.result=-1;}
    else if(stale){r.result=q.op==PollWrite?0:-1;}
    else if(q.op==Connect){
        // Numeric IPv4 is the admitted W5500 endpoint form. A DNS/TLS profile
        // requires its own transport acceptance and may not silently fall back.
        drop();xStreamBufferReset(received);     // nothing from an earlier connection survives
        IPAddress ip;
        if(q.port>0&&q.port<=65535&&ip.fromString(q.host)){
            net.setConnectionTimeout(1000);
            if(net.connect(ip,uint16_t(q.port))){linked.store(true);eth_reap_exempt(net.getSocketNumber());r.result=0;}
        }
    }else if(!linked.load()||!net.connected()){r.result=-1;}
    else if(q.op==Write){r.result=int(net.write(q.bytes,size_t(q.count)));}
    else if(q.op==PollWrite){r.result=net.availableForWrite()>0?1:0;if(!r.result&&!due)return;}
    xQueueOverwrite(responses,&r);pending=false;
}
#else
esp_transport_handle_t mqtt_w5500_transport(){return nullptr;}
void mqtt_w5500_service(){}
#endif
