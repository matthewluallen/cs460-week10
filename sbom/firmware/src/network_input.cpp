#include "network_input.h"
#include "net_types.h"
#include "plc_core.h"
#include "modbus_tcp_server.h"
#include <Arduino.h>

namespace {
constexpr unsigned Slots=3;
struct Guard {Guard(){plc_image_lock();}~Guard(){plc_image_unlock();}};
struct Slot {bool enabled=false;uint32_t revision=0;network_input::Config config;network_input::State state;};
Slot slots[Slots];
NetClient client;
network_input::Config active;
network_input::Frame frame;
uint32_t revision=0,started=0,last_poll[Slots]{},seen_revision[Slots]{};
unsigned active_slot=0,next_slot=0;
uint16_t transaction=0;
bool in_flight=false;
void fail(network_input::Status reason) {
    Guard guard;auto& s=slots[active_slot];
    if(s.enabled&&s.revision==revision)s.state.fail(reason);
}
void close() {client.stop();in_flight=false;}
}
void network_input_configure(unsigned slot,bool enabled,const network_input::Config& config) {
    if(slot>=Slots)return;
    Guard guard;auto& s=slots[slot];++s.revision;s.enabled=enabled&&network_input::valid(config);
    s.config=config;s.state={};
}
network_input::State network_input_snapshot(unsigned slot) {
    Guard guard;return slot<Slots?slots[slot].state:network_input::State{};
}
void network_input_service() {
    const uint32_t now=millis();
    if(in_flight) {
        bool replaced=false;
        {Guard guard;replaced=!slots[active_slot].enabled||slots[active_slot].revision!=revision;}
        if(replaced){close();return;}
        if(now-started>=active.timeout_ms){fail(network_input::Status::IoError);close();return;}
        int available=client.available();
        if(available>0) {
            uint8_t bytes[network_input::ReplyBytes+1];
            int count=client.read(bytes,available>int(sizeof(bytes))?sizeof(bytes):available);
            if(count>0) {
                bool complete=frame.feed(bytes,size_t(count));
                if(frame.invalid){fail(network_input::Status::ProtocolError);close();return;}
                if(complete) {
                    // A library read can consume the entire deadline. Recheck completion time.
                    uint32_t received=millis();
                    {Guard guard;auto& s=slots[active_slot];if(s.enabled&&s.revision==revision) {
                        if(received-started>=active.timeout_ms)s.state.fail(network_input::Status::IoError);
                        else s.state.accept(active,transaction,frame.bytes,frame.used,received);
                    }}
                    close();return;
                }
            }
        }
        if(!client.connected()&&!client.available()){fail(network_input::Status::IoError);close();}
        return;
    }
    bool found=false;
    {Guard guard;
        for(unsigned n=0;n<Slots;++n) {
            unsigned i=(next_slot+n)%Slots;auto& s=slots[i];if(!s.enabled)continue;
            if(seen_revision[i]==s.revision&&now-last_poll[i]<s.config.poll_ms)continue;
            active_slot=i;active=s.config;revision=s.revision;seen_revision[i]=revision;
            last_poll[i]=now;next_slot=(i+1)%Slots;found=true;break;
        }
    }
    if(!found)return;
    started=now;frame={};++transaction;
    // This bounds connection setup only, not a hard real-time guarantee for the driver.
    uint32_t connect_ms=active.timeout_ms<100?active.timeout_ms:100;
#ifdef ENABLE_ETHERNET
    client.setConnectionTimeout(connect_ms);
    int connected=client.connect(IPAddress(active.host),active.port);
#else
    int connected=client.connect(IPAddress(active.host),active.port,connect_ms);
#endif
    if(!connected||millis()-started>=active.timeout_ms){fail(network_input::Status::IoError);close();return;}
    uint8_t request[12];network_input::request(active,transaction,request);
    if(client.write(request,sizeof(request))!=sizeof(request)){fail(network_input::Status::IoError);close();return;}
    in_flight=true;
}
