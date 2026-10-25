#pragma once
#include <stddef.h>
#include <stdint.h>

// Allocation-free DNP3 link/TCP stream foundation. Unknown application behavior
// belongs to the outstation's device profile, not to this byte parser.
namespace dnp3 {
constexpr size_t MAX_USER=250;
constexpr size_t MAX_FRAME=292;
uint16_t crc(const uint8_t* data,size_t size);
size_t encode(uint8_t* out,size_t capacity,uint16_t destination,uint16_t source,
              uint8_t control,const uint8_t* user,size_t length);
struct LinkFrame {
    uint16_t destination=0,source=0;
    uint8_t control=0,user[MAX_USER]={};
    size_t length=0;
};
bool decode(const uint8_t* data,size_t size,LinkFrame& out);
class Stream {
public:
    // Feed one TCP octet. True returns one complete CRC-verified link frame.
    // Invalid input is discarded/resynchronized without producing a command.
    bool push(uint8_t byte,LinkFrame& out);
    void clear(){used_=0;}
    size_t buffered()const{return used_;}
    uint32_t rejected()const{return rejected_;}
private:
    uint8_t bytes_[MAX_FRAME]={};size_t used_=0;uint32_t rejected_=0;
    void discard(size_t count);
};
class Transport {
public:
    enum Result { Waiting, Complete, Rejected };
    Result accept(const LinkFrame& frame,uint64_t now_ms);
    void clear(){used_=0;active_=false;}
    const uint8_t* data()const{return data_;}
    size_t size()const{return used_;}
private:
    uint8_t data_[2048]={},sequence_=0;size_t used_=0;
    uint16_t source_=0,destination_=0;uint64_t last_ms_=0;bool active_=false;
};
}
