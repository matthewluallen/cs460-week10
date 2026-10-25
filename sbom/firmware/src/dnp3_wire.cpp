#include "dnp3_wire.h"
#include <string.h>
namespace dnp3 {
uint16_t crc(const uint8_t* data,size_t size){
    uint16_t c=0;for(size_t i=0;i<size;i++){c^=data[i];for(int b=0;b<8;b++)c=c&1?(c>>1)^0xa6bc:c>>1;}return ~c;
}
static uint16_t word(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static void put(uint8_t* p,uint16_t value){p[0]=uint8_t(value);p[1]=uint8_t(value>>8);}
static size_t wire_size(size_t n){return 10+n+2*((n+15)/16);}
size_t encode(uint8_t* out,size_t capacity,uint16_t destination,uint16_t source,uint8_t control,const uint8_t* user,size_t length){
    if(!out||length>MAX_USER||capacity<wire_size(length)||(!user&&length))return 0;
    out[0]=5;out[1]=100;out[2]=uint8_t(5+length);out[3]=control;
    put(out+4,destination);put(out+6,source);put(out+8,crc(out,8));size_t p=10;
    for(size_t i=0;i<length;i+=16){size_t n=length-i;if(n>16)n=16;memcpy(out+p,user+i,n);put(out+p+n,crc(user+i,n));p+=n+2;}return p;
}
bool decode(const uint8_t* data,size_t size,LinkFrame& out){
    // Decode into a candidate so failure cannot publish partially trusted fields.
    if(!data||size<10||data[0]!=5||data[1]!=100||data[2]<5)return false;
    size_t left=data[2]-5;if(size!=wire_size(left)||word(data+8)!=crc(data,8))return false;
    LinkFrame next;next.destination=word(data+4);next.source=word(data+6);next.control=data[3];next.length=left;
    size_t p=10,u=0;while(left){size_t n=left>16?16:left;if(word(data+p+n)!=crc(data+p,n))return false;memcpy(next.user+u,data+p,n);p+=n+2;u+=n;left-=n;}
    out=next;return true;
}
void Stream::discard(size_t n){if(n>=used_){used_=0;return;}memmove(bytes_,bytes_+n,used_-n);used_-=n;}
bool Stream::push(uint8_t byte,LinkFrame& out){
    if(used_==MAX_FRAME){discard(1);rejected_++;}bytes_[used_++]=byte;
    while(used_){
        if(bytes_[0]!=5){discard(1);continue;}
        if(used_<2)return false;
        if(bytes_[1]!=100){discard(1);continue;}
        if(used_<3)return false;
        if(bytes_[2]<5){discard(1);rejected_++;continue;}
        if(used_<10)return false;
        if(word(bytes_+8)!=crc(bytes_,8)){discard(1);rejected_++;continue;}
        const size_t length=wire_size(bytes_[2]-5);if(used_<length)return false;
        if(decode(bytes_,length,out)){discard(length);return true;}
        // A valid header fixes the link frame boundary even when a data CRC
        // fails. Never reinterpret its corrupted payload as an inner command.
        discard(length);rejected_++;return false;
    }return false;
}
Transport::Result Transport::accept(const LinkFrame& frame,uint64_t now_ms){
    if(!frame.length){clear();return Rejected;}
    uint8_t flags=frame.user[0];bool first=flags&64,final=flags&128;
    if(first){clear();source_=frame.source;destination_=frame.destination;sequence_=flags&63;active_=true;}
    else if(!active_||now_ms<last_ms_||now_ms-last_ms_>2000||source_!=frame.source||destination_!=frame.destination||((sequence_+1)&63)!=(flags&63)){clear();return Rejected;}
    const size_t n=frame.length-1;if(used_+n>sizeof(data_)){clear();return Rejected;}
    memcpy(data_+used_,frame.user+1,n);used_+=n;sequence_=flags&63;last_ms_=now_ms;
    if(final){active_=false;return Complete;}return Waiting;
}
}
