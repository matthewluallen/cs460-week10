#include "network_access.h"
#include "mqtt_runtime.h"
#include "mqtt_transport.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>
#include <string.h>
namespace {
constexpr size_t MAX_PAYLOAD=1024,MAX_TOPIC=191,QueueDepth=8;
struct Message {char topic[MAX_TOPIC+1];uint8_t data[MAX_PAYLOAD];size_t length;};
esp_mqtt_client_handle_t client=nullptr;
QueueHandle_t incoming=nullptr;
MqttMessageHandler handler=nullptr;
std::atomic<bool> connected{false},present{false};
std::atomic<uint32_t> connections{0},disconnections{0},delivered{0},received{0},dropped{0},errors{0};
std::atomic<int> last_id{-1};
// ESP-MQTT serializes callbacks on its own task. Its event memory is transient.
Message assembling{};size_t used=0,expected=0;bool valid=false;
void event(void*,esp_event_base_t,int32_t id,void* data){
    auto e=static_cast<esp_mqtt_event_handle_t>(data);
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
        if(used==expected){assembling.length=used;if(xQueueSend(incoming,&assembling,0)!=pdTRUE)dropped++;else received++;valid=false;}
    }
}
}
bool mqtt_runtime_begin(const char* host,uint16_t port,const char* id,const char* will_topic,const uint8_t* will,size_t will_len,MqttMessageHandler callback){
    if(plc_project_host_acl_enabled())return false;
    if(client||!host||!host[0]||strlen(host)>63||!id||!id[0]||strlen(id)>63||!will_topic||strlen(will_topic)>MAX_TOPIC||will_len>MAX_PAYLOAD||(!will&&will_len)||!port)return false;
    // Depth 8: a phone publishes several topics at once (accel+gyro+orient...). Drops are
    // counted (mqtt status: mqtt_dropped) and each phone point simply waits for its next sample.
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
        esp_transport_destroy(config.network.transport);
#endif
        vQueueDelete(incoming);incoming=nullptr;return false;
    }
    if(esp_mqtt_client_register_event(client,MQTT_EVENT_ANY,event,nullptr)!=ESP_OK||esp_mqtt_client_start(client)!=ESP_OK){
        esp_mqtt_client_destroy(client);client=nullptr;
#ifdef ENABLE_ETHERNET
        esp_transport_destroy(config.network.transport);   // custom W5500 transport is caller-owned; free on this failure path too
#endif
        vQueueDelete(incoming);incoming=nullptr;return false;
    }
    return true;
}
void mqtt_runtime_poll(){if(!incoming)return;Message m;for(unsigned i=0;i<QueueDepth&&xQueueReceive(incoming,&m,0)==pdTRUE;i++)if(handler&&!plc_project_host_acl_enabled())handler(m.topic,m.data,m.length);}
MqttStatus mqtt_runtime_status(){return {client!=nullptr,connected.load(),present.load(),connections.load(),disconnections.load(),delivered.load(),received.load(),dropped.load(),errors.load(),last_id.load()};}
int mqtt_runtime_publish(const char* topic,const uint8_t* data,size_t size,int qos,bool retain){
    {
        PlcNetworkImageGuard guard;
        if(plc_project_host_acl_enabled())return -1;
    }
    if(!client||!topic||!topic[0]||strlen(topic)>MAX_TOPIC||strchr(topic,'#')||strchr(topic,'+')||size>MAX_PAYLOAD||(!data&&size)||qos<0||qos>2)return -1;
    // Admission above is the policy decision. Previously admitted bytes may
    // remain queued/in flight after policy activation. Never hold the PLC image
    // lock across an ESP-MQTT API: its mutex owner may await W5500 service, and
    // that service also checks the image policy (otherwise a lock cycle results).
    return esp_mqtt_client_enqueue(client,topic,reinterpret_cast<const char*>(data),int(size),qos,retain,true);
}
int mqtt_runtime_subscribe(const char* topic,int qos){if(plc_project_host_acl_enabled())return -1;if(!client||!topic||!topic[0]||strlen(topic)>MAX_TOPIC||qos<0||qos>2)return -1;return esp_mqtt_client_subscribe(client,topic,qos);}
int mqtt_runtime_unsubscribe(const char* topic){if(!client||!topic||!topic[0]||strlen(topic)>MAX_TOPIC)return -1;return esp_mqtt_client_unsubscribe(client,topic);}
