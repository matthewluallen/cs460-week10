#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
namespace recording {
constexpr size_t PayloadMax=8192, FrameMax=PayloadMax+192;
constexpr uint32_t SendDeadlineMs=6000;
inline bool request_id(const char* s) {
    if(!s || strlen(s)!=32)return false;
    for(unsigned i=0;i<32;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
    return true;
}
inline uint32_t crc32(const char* p,size_t n) {
    uint32_t c=~0u;
    for(size_t i=0;i<n;++i){c^=uint8_t(p[i]);for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));}
    return ~c;
}
inline size_t frame(char* out,size_t capacity,const char* request,uint32_t sequence,const char* payload,size_t n) {
    if(!request_id(request)||!n||n>PayloadMax||capacity<n+192)return 0;
    int head=snprintf(out,capacity,"\n@TBD1 %s %lu %u %08lx\n",request,(unsigned long)sequence,unsigned(n),(unsigned long)crc32(payload,n));
    if(head<0 || size_t(head)>=capacity)return 0;
    memcpy(out+head,payload,n);
    int tail=snprintf(out+head+n,capacity-head-n,"\n@END1 %s %lu\n",request,(unsigned long)sequence);
    if(tail<0||size_t(tail)>=capacity-head-n)return 0;
    return size_t(head)+n+size_t(tail);
}
// Sink is the USB boundary; no waiting here. Each call issues at most four
// short writes. Timeout discards the remainder without a completion marker.
struct Sender {
    size_t size=0,pos=0;uint32_t began=0;bool failed=false;
    bool active() const {return size!=0;}
    bool start(size_t n,uint32_t now) {if(active()||!n||n>FrameMax)return false;size=n;pos=0;began=now;failed=false;return true;}
    template<class Sink> void poll(Sink& sink,const char* data,uint32_t now) {
        if(!active())return;
        if(uint32_t(now-began)>=SendDeadlineMs){size=0;failed=true;return;}
        for(unsigned i=0;i<4 && pos<size;++i){int available=sink.availableForWrite();if(available<=0)return;
            size_t n=size-pos;if(n>32)n=32;if(n>size_t(available))n=available;
            size_t sent=sink.write(reinterpret_cast<const uint8_t*>(data+pos),n);
            if(sent>n){size=0;failed=true;return;}pos+=sent;if(!sent)return;
        }
        if(pos==size)size=0;
    }
};
}
