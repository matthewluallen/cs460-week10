#include "display_capture_console.h"
#include "display_capture_protocol.h"
#include "display_ui.h"
#include "record_frame.h"
#include <Arduino.h>
#include <cstdio>
namespace {
recording::Sender sender;
char wire[1024];
display_capture::Ticket active_ticket{};
bool failed=false;
void reply(const char* body){
 size_t n=std::strlen(body);if(n+2>=sizeof(wire)){failed=true;return;}
 std::memmove(wire+1,body,n);wire[0]='\n';wire[n+1]='\n';
 failed=!sender.start(n+2,millis());
}
}
bool display_capture_console_busy(){return sender.active();}
bool display_capture_console_failed(){return failed;}
void display_capture_console_poll(){
 if(!sender.active())return;
 if(Serial.available()&&Serial.peek()==3){
  Serial.read();sender.size=0;failed=true;display_ui_capture_cancel(active_ticket);return;
 }
 sender.poll(Serial,wire,millis());
 if(sender.failed){failed=true;display_ui_capture_cancel(active_ticket);}
}
bool display_capture_console_handle(const char* line){
 using namespace display_capture;
 if(std::strcmp(line,"screen")&&std::strncmp(line,"screen ",7))return false;
 if(sender.active())return true;
 Command c;char response[960];
 if(!parse(line,c)){reply("{\"screen\":\"reply\",\"schema\":1,\"result\":\"invalid\"}");return true;}
 Manifest m;uint8_t bytes[MaxChunk];Result result;
 switch(c.kind){
 case CommandKind::Request:result=display_ui_capture_request(c.ticket);break;
 case CommandKind::Manifest:result=display_ui_capture_manifest(c.ticket,m);break;
 case CommandKind::Read:result=display_ui_capture_read(c.ticket,c.offset,c.length,bytes,sizeof(bytes));break;
 default:result=display_ui_capture_cancel(c.ticket);break;
 }
 active_ticket=c.ticket;
 char boot[33],sequence[17];for(unsigned i=0;i<16;++i)std::snprintf(boot+2*i,3,"%02x",c.ticket.boot[i]);
 std::snprintf(sequence,sizeof(sequence),"%016llx",static_cast<unsigned long long>(c.ticket.sequence));
 int n=std::snprintf(response,sizeof(response),"{\"screen\":\"reply\",\"schema\":1,\"result\":\"%s\",\"boot\":\"%s\",\"frame\":\"%s\"",result_name(result),boot,sequence);
 if(result==Result::Unavailable){n+=std::snprintf(response+n,sizeof(response)-n,",\"availability\":\"%s\"",display_ui_capture_status());}
 if(result==Result::Ok&&c.kind==CommandKind::Manifest){
  n+=std::snprintf(response+n,sizeof(response)-n,",\"width\":%u,\"height\":%u,\"rotation\":%u,\"format\":\"rgb565le\",\"bytes\":%u,\"rendered_us\":\"%llu\",\"clock\":\"controller_boot_us\",\"source\":\"software_submitted_frame\",\"crc32\":\"%08lx\"",m.width,m.height,m.rotation,unsigned(m.bytes),static_cast<unsigned long long>(m.rendered_us),static_cast<unsigned long>(m.crc32));
 }
 if(result==Result::Ok&&c.kind==CommandKind::Read){
  n+=std::snprintf(response+n,sizeof(response)-n,",\"offset\":%u,\"length\":%u,\"hex\":\"",unsigned(c.offset),unsigned(c.length));
  for(size_t i=0;i<c.length;++i){std::snprintf(response+n,3,"%02x",bytes[i]);n+=2;}response[n++]='"';
 }
 response[n++]='}';response[n]=0;reply(response);return true;
}
