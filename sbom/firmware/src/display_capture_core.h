// Frozen software-render snapshots; no hardware, allocation, I/O or control writes.
// Caller owns storage and serializes access on the display task. Pixels supplied
// to publish must be the completed render surface submitted to the panel.
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace display_capture {
using Boot = std::array<uint8_t,16>;
constexpr size_t MaxBytes=240u*135u*2u;
constexpr size_t MaxChunk=256;
constexpr uint64_t LifetimeUs=30000000; // fixed lease; readers cannot extend it
struct Ticket { Boot boot{}; uint64_t sequence=0; };
struct Manifest {
    Ticket ticket{};
    uint16_t width=0,height=0;
    uint8_t rotation=0;
    size_t bytes=0;
    uint64_t rendered_us=0;
    uint32_t crc32=0; // corruption detection, never authentication
};
enum class State { Idle, Pending, Ready };
enum class Result { Ok, Unavailable, Busy, WrongFrame, Expired, NotReady, Invalid };
inline uint32_t checksum(const uint8_t* p,size_t size){
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<size;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));}
    return ~crc;
}
class Snapshot {
    uint8_t* storage_; size_t capacity_; Boot boot_;
    State state_=State::Idle;
    uint64_t sequence_=0,requested_=0;
    Manifest manifest_{};
    bool valid_boot()const {for(auto b:boot_)if(b)return true;return false;}
    bool matches(const Ticket& t)const{return t.boot==boot_&&t.sequence!=0&&t.sequence==sequence_;}
    Result check(const Ticket& t,uint64_t now){
        if(!matches(t))return Result::WrongFrame;
        if(state_==State::Idle)return Result::Unavailable;
        if(now<requested_||now-requested_>=LifetimeUs){clear();return Result::Expired;}
        return Result::Ok;
    }
    void clear(){state_=State::Idle;manifest_={};}
public:
    Snapshot(uint8_t* storage,size_t capacity,Boot boot):storage_(storage),capacity_(capacity),boot_(boot){}
    // Copying would create two owners of the same mutable storage.
    Snapshot(const Snapshot&)=delete;
    Snapshot& operator=(const Snapshot&)=delete;
    Result request(uint64_t now,Ticket& out){
        if(!storage_||capacity_<MaxBytes||!valid_boot())return Result::Unavailable;
        if(state_!=State::Idle){
            if(now<requested_||now-requested_>=LifetimeUs)clear();else return Result::Busy;
        }
        if(sequence_==UINT64_MAX)return Result::Unavailable; // no ticket reuse
        ++sequence_;requested_=now;state_=State::Pending;out={boot_,sequence_};return Result::Ok;
    }
    Result publish(const Ticket& t,uint64_t now,const uint8_t* rgb565le,size_t bytes,
                   uint16_t width,uint16_t height,uint8_t rotation,uint64_t rendered_us){
        auto result=check(t,now);if(result!=Result::Ok)return result;
        if(state_!=State::Pending)return Result::Busy;
        if(!rgb565le||rotation>3||!((width==240&&height==135)||(width==135&&height==240))||
           bytes!=size_t(width)*height*2||bytes>capacity_||rendered_us<requested_||rendered_us>now)return Result::Invalid;
        // Fixed upper bound. Target memory/copy/CRC duration needs qualification.
        std::memmove(storage_,rgb565le,bytes);
        manifest_={t,width,height,rotation,bytes,rendered_us,checksum(storage_,bytes)};
        state_=State::Ready;return Result::Ok;
    }
    Result manifest(const Ticket& t,uint64_t now,Manifest& out){
        auto result=check(t,now);if(result!=Result::Ok)return result;
        if(state_!=State::Ready)return Result::NotReady;
        out=manifest_;return Result::Ok;
    }
    Result read(const Ticket& t,uint64_t now,size_t offset,size_t length,uint8_t* out,size_t capacity){
        auto result=check(t,now);if(result!=Result::Ok)return result;
        if(state_!=State::Ready)return Result::NotReady;
        if(!out||!length||length>MaxChunk||length>capacity||offset>manifest_.bytes||length>manifest_.bytes-offset)return Result::Invalid;
        std::memmove(out,storage_+offset,length);return Result::Ok;
    }
    Result cancel(const Ticket& t,uint64_t now){auto r=check(t,now);if(r==Result::Ok)clear();return r;}
};
}
