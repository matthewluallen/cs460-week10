#include "mqtt_transport.h"
#ifdef ENABLE_ETHERNET
#include "net_types.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <atomic>
#include <string.h>
namespace {
constexpr size_t CAPACITY=1536;
enum Operation { Connect, Read, Write, PollRead, PollWrite, Close };
struct Request { uint32_t id;Operation op;int count,port;int64_t deadline;char host[64];uint8_t bytes[CAPACITY]; };
struct Response { uint32_t id;int result;uint8_t bytes[CAPACITY]; };
QueueHandle_t requests=nullptr,responses=nullptr;
SemaphoreHandle_t caller=nullptr;
NetClient net;
std::atomic<bool> initialized{false};
uint32_t serial=0;
int64_t clock_ms(){return esp_timer_get_time()/1000;}
int exchange(Operation op,const char* input,char* output,int n,int timeout,const char* host=nullptr,int port=0){
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
            if(xQueueReceive(responses,&r,pdMS_TO_TICKS(10))==pdTRUE&&r.id==q.id){
                result=r.result;if(output&&result>0&&result<=q.count)memcpy(output,r.bytes,result);break;
            }
        }
    }
    xSemaphoreGive(caller);return result;
}
int connect_cb(esp_transport_handle_t,const char* host,int port,int timeout){return exchange(Connect,nullptr,nullptr,0,timeout,host,port);}
int read_cb(esp_transport_handle_t,char* b,int n,int timeout){return exchange(Read,nullptr,b,n,timeout);}
int write_cb(esp_transport_handle_t,const char* b,int n,int timeout){return exchange(Write,b,nullptr,n,timeout);}
int poll_read_cb(esp_transport_handle_t,int timeout){return exchange(PollRead,nullptr,nullptr,0,timeout);}
int poll_write_cb(esp_transport_handle_t,int timeout){return exchange(PollWrite,nullptr,nullptr,0,timeout);}
int close_cb(esp_transport_handle_t){return exchange(Close,nullptr,nullptr,0,1000);}
int destroy_cb(esp_transport_handle_t){return close_cb(nullptr);}
}
esp_transport_handle_t mqtt_w5500_transport(){
    if(!initialized.load()){
        requests=xQueueCreate(1,sizeof(Request));responses=xQueueCreate(1,sizeof(Response));caller=xSemaphoreCreateMutex();
        if(!requests||!responses||!caller){if(requests)vQueueDelete(requests);if(responses)vQueueDelete(responses);if(caller)vSemaphoreDelete(caller);requests=nullptr;responses=nullptr;caller=nullptr;return nullptr;}
        initialized.store(true);
    }
    esp_transport_handle_t t=esp_transport_init();if(!t)return nullptr;
    esp_transport_set_func(t,connect_cb,read_cb,write_cb,close_cb,poll_read_cb,poll_write_cb,destroy_cb);
    esp_transport_set_default_port(t,1883);return t;
}
void mqtt_w5500_service(){
    if(!initialized.load())return;
    static Request q;static bool pending=false;
    if(!pending){if(xQueueReceive(requests,&q,0)!=pdTRUE)return;pending=true;}
    Response r{};r.id=q.id;r.result=-1;
    if(q.op==Close){net.stop();r.result=0;}
    else if(clock_ms()>=q.deadline){r.result=(q.op==Read||q.op==PollRead||q.op==PollWrite)?0:-1;}
    else if(q.op==Connect){
        // Numeric IPv4 is the admitted W5500 endpoint form. A DNS/TLS profile
        // requires its own transport acceptance and may not silently fall back.
        IPAddress ip;
        if(q.port>0&&q.port<=65535&&ip.fromString(q.host)){
            net.stop();net.setConnectionTimeout(1000);r.result=net.connect(ip,uint16_t(q.port))?0:-1;
        }
    }else if(!net.connected()){r.result=-1;}
    else if(q.op==Write){r.result=int(net.write(q.bytes,size_t(q.count)));}
    else if(q.op==PollWrite){r.result=net.availableForWrite()>0?1:0;if(!r.result)return;}
    else {
        const int available=net.available();if(!available)return;
        if(q.op==PollRead)r.result=1;
        else if(q.op==Read)r.result=net.read(r.bytes,size_t(available<q.count?available:q.count));
    }
    xQueueOverwrite(responses,&r);pending=false;
}
#else
esp_transport_handle_t mqtt_w5500_transport(){return nullptr;}
void mqtt_w5500_service(){}
#endif
