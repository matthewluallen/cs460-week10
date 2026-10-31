#include "network_access.h"
#include "mqtt_runtime.h"
#include "mqtt_transport.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include <atomic>
#include <string.h>
namespace {
constexpr size_t MAX_PAYLOAD=1024,MAX_TOPIC=191,QueueDepth=8;
struct Message {char topic[MAX_TOPIC+1];uint8_t data[MAX_PAYLOAD];size_t length;};
// Lifecycle. Only the loop task calls begin/stop/poll/publish/subscribe. A client is
// torn down on a short-lived helper task (esp_mqtt_client_destroy waits for the MQTT
// task to finish its current, bounded W5500 exchange and reconnect wait, which can
// take several seconds). The loop - and with it the PLC scan - never blocks on it. A
// new client starts only after the old one is fully destroyed (state Idle again).
enum State : uint8_t { Idle, Running, Stopping };
std::atomic<uint8_t> state{Idle};
esp_mqtt_client_handle_t client=nullptr;           // loop task only
std::atomic<esp_mqtt_client_handle_t> active{nullptr}; // read by the MQTT task's event handler
QueueHandle_t incoming=nullptr;                     // loop task only; the handler gets its own copy
esp_mqtt_client_handle_t retiring=nullptr;QueueHandle_t retiring_queue=nullptr;
MqttMessageHandler handler=nullptr;
std::atomic<bool> connected{false},present{false};
// Counters are cumulative since boot across clients (the Sparkplug layer resubscribes
// whenever `connections` changes, so it must never go backwards).
std::atomic<uint32_t> connections{0},disconnections{0},delivered{0},received{0},dropped{0},errors{0},clients{0};
std::atomic<int> last_id{-1};
// ESP-MQTT serializes callbacks on its own task. Its event memory is transient. Two
// MQTT tasks never coexist (see the lifecycle above), so this scratch is single-owner.
Message assembling{};size_t used=0,expected=0;bool valid=false;
// When a burst outruns the loop task the queue is full: discard the OLDEST queued message
// and keep the new one. For phone points and setpoints the newest value is the one that
// matters (dropping the newest left a burst's older value standing as "fresh" while the
// latest was lost). `received` counts messages queued, `dropped` everything discarded.
Message discard{};
void event(void* arg,esp_event_base_t,int32_t id,void* data){
    auto e=static_cast<esp_mqtt_event_handle_t>(data);
    // A client that is being retired may still report a late event; ignore it so its
    // state and messages never leak into the next client's status or queue.
    if(!e||e->client!=active.load())return;
    QueueHandle_t queue=static_cast<QueueHandle_t>(arg);
    if(id==MQTT_EVENT_CONNECTED){present.store(e->session_present);connected.store(true);connections++;}
    else if(id==MQTT_EVENT_DISCONNECTED){connected.store(false);present.store(false);valid=false;disconnections++;}
    else if(id==MQTT_EVENT_PUBLISHED){last_id.store(e->msg_id);delivered++;}
    else if(id==MQTT_EVENT_ERROR){errors++;}
    else if(id==MQTT_EVENT_DATA){
        if(e->current_data_offset==0){
            valid=e->topic_len>0&&e->topic_len<=int(MAX_TOPIC)&&e->total_data_len>=0&&e->total_data_len<=int(MAX_PAYLOAD);
            used=0;expected=valid?size_t(e->total_data_len):0;
            if(valid){memcpy(assembling.topic,e->topic,e->topic_len);assembling.topic[e->topic_len]=0;}
            else dropped++;
        }
        if(!valid)return;
        if(e->current_data_offset<0||size_t(e->current_data_offset)!=used||e->data_len<0||size_t(e->data_len)>expected-used){valid=false;dropped++;return;}
        if(e->data_len)memcpy(assembling.data+used,e->data,e->data_len);
        used+=size_t(e->data_len);
        if(used==expected){
            assembling.length=used;
            if(xQueueSend(queue,&assembling,0)!=pdTRUE){
                if(xQueueReceive(queue,&discard,0)==pdTRUE)dropped++;
                if(xQueueSend(queue,&assembling,0)==pdTRUE)received++;else dropped++;
            }else received++;
            valid=false;
        }
    }
}
void destroy_retiring(){
    // Per mqtt_client.h, esp_mqtt_client_destroy stops the MQTT task and also destroys
    // the custom transport handed to it in network.transport (the W5500 adapter).
    if(retiring)esp_mqtt_client_destroy(retiring);
    if(retiring_queue)vQueueDelete(retiring_queue);
    retiring=nullptr;retiring_queue=nullptr;
    state.store(Idle);
}
void retire_task(void*){destroy_retiring();vTaskDelete(nullptr);}
}
bool mqtt_runtime_begin(const char* host,uint16_t port,const char* id,const char* will_topic,const uint8_t* will,size_t will_len,MqttMessageHandler callback){
    if(plc_project_host_acl_enabled())return false;
    if(state.load()!=Idle)return false;   // stop the previous client first (mqtt_runtime_stop)
    if(!host||!host[0]||strlen(host)>63||!id||!id[0]||strlen(id)>63||!will_topic||strlen(will_topic)>MAX_TOPIC||will_len>MAX_PAYLOAD||(!will&&will_len)||!port)return false;
    incoming=xQueueCreate(QueueDepth,sizeof(Message));if(!incoming)return false;
    handler=callback;
    esp_mqtt_client_config_t config{};
    config.broker.address.hostname=host;config.broker.address.port=port;config.broker.address.transport=MQTT_TRANSPORT_OVER_TCP;
    config.credentials.client_id=id;
    config.session.protocol_ver=MQTT_PROTOCOL_V_3_1_1;
    config.session.disable_clean_session=true;
    config.session.keepalive=30;
    config.session.last_will.topic=will_topic;config.session.last_will.msg=reinterpret_cast<const char*>(will);
    config.session.last_will.msg_len=int(will_len);config.session.last_will.qos=1;config.session.last_will.retain=false;
    // MQTT3.1.1 retransmits unacknowledged publications on a resumed connection.
    // The pinned SDK also supports in-session retransmit; keep that disabled for
    // the bounded lab duration and record the implementation's finite threshold.
    config.session.message_retransmit_timeout=0x7fffffff;
    config.network.reconnect_timeout_ms=5000;config.network.timeout_ms=2000;
    config.buffer.size=1536;config.buffer.out_size=1536;config.task.stack_size=12288;
    config.outbox.limit=16384;
#ifdef ENABLE_ETHERNET
    config.network.transport=mqtt_w5500_transport();
    if(!config.network.transport){vQueueDelete(incoming);incoming=nullptr;return false;}
#endif
    client=esp_mqtt_client_init(&config);
    if(!client){
#ifdef ENABLE_ETHERNET
        esp_transport_destroy(config.network.transport);   // init failed: the transport was never handed over
#endif
        vQueueDelete(incoming);incoming=nullptr;return false;
    }
    active.store(client);
    if(esp_mqtt_client_register_event(client,MQTT_EVENT_ANY,event,incoming)!=ESP_OK||esp_mqtt_client_start(client)!=ESP_OK){
        active.store(nullptr);
        esp_mqtt_client_destroy(client);client=nullptr;     // also destroys the custom transport (mqtt_client.h)
        vQueueDelete(incoming);incoming=nullptr;return false;
    }
    clients++;
    state.store(Running);
    return true;
}
void mqtt_runtime_stop(){
    if(state.load()!=Running)return;      // Idle: nothing to do; Stopping: already under way
    active.store(nullptr);                // the handler ignores everything from here on
    connected.store(false);present.store(false);
    retiring=client;retiring_queue=incoming;client=nullptr;incoming=nullptr;handler=nullptr;
    state.store(Stopping);
    if(xTaskCreate(retire_task,"mqtt_retire",8192,nullptr,1,nullptr)!=pdPASS){
        // No memory for the helper: tear down here. Feed the loop watchdog first; the
        // stop is bounded by the transport deadlines plus one reconnect wait.
        esp_task_wdt_reset();
        destroy_retiring();
    }
}
bool mqtt_runtime_idle(){return state.load()==Idle;}
const char* mqtt_runtime_state(){
    const uint8_t s=state.load();
    return s==Stopping?"stopping":s==Idle?"idle":connected.load()?"connected":"connecting";
}
void mqtt_runtime_poll(){
    if(state.load()!=Running||!incoming)return;
    Message m;
    for(unsigned i=0;i<QueueDepth&&xQueueReceive(incoming,&m,0)==pdTRUE;i++)
        if(handler&&!plc_project_host_acl_enabled())handler(m.topic,m.data,m.length);
}
MqttStatus mqtt_runtime_status(){return {state.load()==Running,connected.load(),present.load(),connections.load(),disconnections.load(),delivered.load(),received.load(),dropped.load(),errors.load(),last_id.load(),clients.load()};}
int mqtt_runtime_publish(const char* topic,const uint8_t* data,size_t size,int qos,bool retain){
    {
        PlcNetworkImageGuard guard;
        if(plc_project_host_acl_enabled())return -1;
    }
    if(state.load()!=Running||!client||!topic||!topic[0]||strlen(topic)>MAX_TOPIC||strchr(topic,'#')||strchr(topic,'+')||size>MAX_PAYLOAD||(!data&&size)||qos<0||qos>2)return -1;
    // Admission above is the policy decision. Previously admitted bytes may
    // remain queued/in flight after policy activation. Never hold the PLC image
    // lock across an ESP-MQTT API: its mutex owner may await W5500 service, and
    // that service also checks the image policy (otherwise a lock cycle results).
    return esp_mqtt_client_enqueue(client,topic,reinterpret_cast<const char*>(data),int(size),qos,retain,true);
}
int mqtt_runtime_subscribe(const char* topic,int qos){if(plc_project_host_acl_enabled())return -1;if(state.load()!=Running||!client||!topic||!topic[0]||strlen(topic)>MAX_TOPIC||qos<0||qos>2)return -1;return esp_mqtt_client_subscribe(client,topic,qos);}
int mqtt_runtime_unsubscribe(const char* topic){if(state.load()!=Running||!client||!topic||!topic[0]||strlen(topic)>MAX_TOPIC)return -1;return esp_mqtt_client_unsubscribe(client,topic);}
