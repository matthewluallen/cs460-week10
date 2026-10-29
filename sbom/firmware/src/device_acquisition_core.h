// Per-route sensor state machines. Conversion delays never sleep in tick().
#pragma once
#include "device_inventory_core.h"
#include <math.h>
#include "vl53l1x_init.h"
#include "vl53l1x_poll.h"

namespace acquisition {
enum class Kind : uint8_t { Env, Sgp, Heart, Io, Ncir, Tof };
// 'Removed' is appended last so the existing on-wire quality ordinals are unchanged.
// It denotes a unit that was present and sampling and has since been unplugged
// (auto age-out), distinct from IoError (a unit that is wired but failing I/O).
enum class Quality : uint8_t { Awaiting, Valid, Warmup, IoError, CrcError, Overflow, Stale, Paused, SensorError, Removed };
inline const char* name(Quality q) {
    switch(q) {
    case Quality::Valid:return "valid"; case Quality::Warmup:return "warmup";
    case Quality::IoError:return "io_error"; case Quality::CrcError:return "crc_error";
    case Quality::Overflow:return "overflow"; case Quality::Stale:return "stale";
    case Quality::SensorError:return "sensor_error";
    case Quality::Removed:return "removed";
    case Quality::Paused:return "paused"; default:return "awaiting";
    }
}
inline bool due(uint32_t now, uint32_t deadline) { return int32_t(now-deadline)>=0; }
struct Values {
    float temp_c=0, rh_pct=0, pressure_pa=0, gas_ohm=0;
    int32_t ambient_centi_c=0, object_centi_c=0;
    int32_t distance_mm=-1;uint8_t range_status=255;
    bool gas_valid=false;
    uint8_t env_status=0,env_index=0;
    uint8_t env_sample_status=0,env_sample_index=0,env_gas_index=0,env_res_heat=0,env_idac=0,env_gas_wait=0;
    uint16_t eco2_ppm=0, tvoc_ppb=0, ir=0, red=0;
    uint8_t fifo_samples=0, io_status=0;
    int16_t mv1=0, mv2=0; int32_t ua1=0, ua2=0;
};
struct Slot {
    devices::Device device;
    Kind kind=Kind::Env;
    Quality quality=Quality::Awaiting;
    Values value;
    vl53l1x::Initializer tof_init;vl53l1x::Poller tof_poll;
    uint32_t next=0, started=0, warmup=0, sampled=0, sequence=0, errors=0, losses=0;
    uint8_t phase=0;
    bool has_sample=false;
    uint32_t max_age() const { return kind==Kind::Heart?500:kind==Kind::Io?1000:3000; }
    Quality current(uint32_t now, bool enabled) const {
        if(!enabled)return Quality::Paused;
        if((quality==Quality::Valid || quality==Quality::Warmup) &&
           (!has_sample || now-sampled>max_age()))return Quality::Stale;
        return quality;
    }
};
// Bus has no implicit delay. Caller selects the route around each backend call.
struct Backend {
    virtual ~Backend()=default;
    virtual devices::Bus& bus(bool internal)=0;
    virtual uint32_t clock_ms()=0;
    virtual bool env_init(size_t index, const devices::Device& d)=0;
    virtual bool env_start(size_t index, uint32_t& wait_ms)=0;
    // 1=fresh compensated field, 0=not ready, -1=bus/format error.
    virtual int env_read(size_t index, Values& v)=0;
};
class Engine {
    static bool route(devices::Bus& b,const devices::Device& d,bool open) {
        if(!d.mux)return true;
        uint8_t v=open?uint8_t(1u<<d.channel):0, read=255;
        return b.write(d.mux,&v,1) && b.read(d.mux,&read,1) && read==v;
    }
    static bool wr(devices::Bus& b,uint8_t a,uint8_t reg,uint8_t v) {
        uint8_t p[]={reg,v};return b.write(a,p,2);
    }
    static bool cmd(devices::Bus& b,uint8_t a,uint16_t c) {
        return wr(b,a,uint8_t(c>>8),uint8_t(c));
    }
    static uint16_t be16(const uint8_t* p) { return uint16_t(p[0])<<8|p[1]; }
    static uint32_t le32(const uint8_t* p) {
        return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
    }
    // Grace past a slot's freshness window before a unit that has stopped
    // responding is treated as physically removed rather than momentarily
    // glitching. max_age() + this must clear a few retry cycles.
    static constexpr uint32_t RetireGraceMs=3000;
    static void fail(Slot& s,Quality q,uint32_t now) {
        ++s.errors;s.phase=0;s.started=0;
        // Auto age-out. A unit that produced valid samples and has now been
        // unreadable well past its freshness window has been unplugged: report
        // it as 'removed' (not a hard io_error) and probe gently. A re-inserted
        // unit recovers on the next successful identity(), which restores Valid.
        // Only I/O-class failure (no ACK / wrong identity) retires a slot; data
        // faults (CRC / overflow / sensor) keep their own quality. A slot that
        // was never present (no sample yet) still latches io_error, so a genuine
        // wiring/config mismatch stays visible instead of silently disappearing.
        if(q==Quality::IoError &&
           (s.quality==Quality::Removed ||
            (s.has_sample && now-s.sampled>s.max_age()+RetireGraceMs))) {
            s.quality=Quality::Removed;s.has_sample=false;s.next=now+2000;return;
        }
        s.quality=q;s.next=now+1000;
    }
    static void sample(Slot& s,uint32_t now,Quality q=Quality::Valid) {
        s.has_sample=true;s.sampled=now;++s.sequence;s.quality=q;
    }
    // Verify the known family before any re-initialization write. A fresh
    // maintenance scan is required to change the declared type/serial binding.
    static bool identity(devices::Bus& b,const Slot& s) {
        uint8_t p[3]{};const auto& d=s.device;
        if(s.kind==Kind::Env) return b.reg(d.address,0xd0,p,1)&&p[0]==d.part &&
            b.reg(d.address,0xf0,p,1)&&p[0]==d.revision;
        if(s.kind==Kind::Heart)return b.reg(d.address,0xff,p,1)&&p[0]==0x11;
        if(s.kind==Kind::Io)return b.reg(d.address,0xff,p,1)&&p[0]==d.address &&
            b.reg(d.address,0xfe,p,1)&&p[0]==d.revision;
        return true; // SGP serial verification has its own asynchronous phases.
    }
    void step(size_t i,Backend& hw,uint32_t now) {
        Slot& s=slots[i];auto& b=hw.bus(s.device.bus);uint8_t a=s.device.address,p[64]{};
        if(s.phase==0 && !identity(b,s)){fail(s,Quality::IoError,now);return;}
        if(s.kind==Kind::Env) {
            if(s.phase==0){if(!hw.env_init(i,s.device)){fail(s,Quality::IoError,now);return;}s.phase=1;s.next=now;}
            else if(s.phase==1){uint32_t wait=0;if(!hw.env_start(i,wait)){fail(s,Quality::IoError,now);return;}
                s.started=hw.clock_ms();s.next=s.started+wait;s.phase=2;}
            else {int result=hw.env_read(i,s.value);
                if(result<0){fail(s,Quality::IoError,now);return;}
                if(!result){if(now-s.started>1000)fail(s,Quality::Stale,now);else s.next=now+10;return;}
                sample(s,hw.clock_ms());s.phase=1;s.next=s.started+1000;}
        } else if(s.kind==Kind::Sgp) {
            if(s.phase==0){if(!cmd(b,a,0x3682)){fail(s,Quality::IoError,now);return;}s.phase=1;s.next=hw.clock_ms()+11;}
            else if(s.phase==1){
                if(!b.read(a,p,9)){fail(s,Quality::IoError,now);return;}
                uint64_t serial=0;for(int j=0;j<9;j+=3){if(devices::crc8(p+j,2)!=p[j+2]){fail(s,Quality::CrcError,now);return;}
                    serial=(serial<<16)|be16(p+j);}
                if(!s.device.unique_serial || serial!=s.device.serial){fail(s,Quality::IoError,now);return;}
                if(!cmd(b,a,0x2003)){fail(s,Quality::IoError,now);return;}s.warmup=hw.clock_ms();s.next=s.warmup+11;s.phase=2;
            } else if(s.phase==2){
                // Missing a complete one-second interval invalidates the algorithm history.
                if(s.started && now-s.started>1500){fail(s,Quality::Stale,now);s.started=0;return;}
                if(!cmd(b,a,0x2008)){fail(s,Quality::IoError,now);return;}
                s.started=hw.clock_ms();s.next=s.started+13;s.phase=3;
            } else {
                if(!b.read(a,p,6)){fail(s,Quality::IoError,now);return;}
                if(devices::crc8(p,2)!=p[2] || devices::crc8(p+3,2)!=p[5]){fail(s,Quality::CrcError,now);s.started=0;return;}
                s.value.eco2_ppm=be16(p);s.value.tvoc_ppb=be16(p+3);
                sample(s,hw.clock_ms(),now-s.warmup<15000?Quality::Warmup:Quality::Valid);
                s.next=s.started+1000;s.phase=2;
            }
        } else if(s.kind==Kind::Heart) {
            if(s.phase==0){if(!wr(b,a,6,0x40)){fail(s,Quality::IoError,now);return;}s.started=now;s.phase=1;s.next=now+10;}
            else if(s.phase==1){
                if(!b.reg(a,6,p,1)){fail(s,Quality::IoError,now);return;}
                if(p[0]&0x40){if(now-s.started>100)fail(s,Quality::IoError,now);else s.next=now+10;return;}
                // 50 Hz, 16-bit / 1600 us, raw red+IR, both LEDs 4.4 mA.
                if(!wr(b,a,1,0)||!wr(b,a,7,0x43)||!wr(b,a,9,0x11)||
                   !wr(b,a,2,0)||!wr(b,a,3,0)||!wr(b,a,4,0)||!wr(b,a,6,3)){
                    fail(s,Quality::IoError,now);return;}
                s.phase=2;s.started=hw.clock_ms();s.next=now+40;
            } else {
                if(!b.reg(a,2,p,3)){fail(s,Quality::IoError,now);return;}
                if(p[1]&15){s.losses+=p[1]&15;fail(s,Quality::Overflow,now);return;}
                if(now-s.started>=320){fail(s,Quality::Stale,now);return;}
                unsigned n=(p[0]-p[2])&15;
                if(!n){s.next=now+20;return;}
                if(!b.reg(a,5,p,n*4)){fail(s,Quality::IoError,now);return;}
                s.value.ir=be16(p+(n-1)*4);s.value.red=be16(p+(n-1)*4+2);s.value.fifo_samples=n;
                sample(s,hw.clock_ms());s.started=hw.clock_ms();s.next=now+40;
            }
        } else if(s.kind==Kind::Tof) {
            auto clock=[&](){return hw.clock_ms();};
            if(s.phase==0){s.tof_init.begin(now);s.quality=Quality::Awaiting;s.phase=1;}
            if(s.phase==1){
                auto result=s.tof_init.tick(b,a,clock);
                if(result==vl53l1x::Initializer::Status::Ready){s.tof_poll.arm(clock(),1000);s.phase=2;}
                else if(result!=vl53l1x::Initializer::Status::Waiting){fail(s,Quality::IoError,clock());return;}
                s.next=clock()+2;return;
            }
            auto result=s.tof_poll.poll(b,a,clock);
            if(result==vl53l1x::PollState::Waiting){s.next=clock()+5;return;}
            if(result!=vl53l1x::PollState::Complete){
                fail(s,result==vl53l1x::PollState::Timeout?Quality::Stale:Quality::IoError,clock());return;
            }
            auto m=s.tof_poll.measurement;
            s.value.distance_mm=m.distance_mm;s.value.range_status=m.raw_status;
            sample(s,s.tof_poll.completed_ms,m.valid()?Quality::Valid:Quality::SensorError);
            s.tof_poll.arm(clock(),1000);s.next=clock()+5;
        } else if(s.kind==Kind::Ncir) {
            // Re-read reported identity each polling cycle. No initialization writes.
            const auto id=mlx90614::read_identity(b,a);
            if(id.status!=mlx90614::Status::Valid || !s.device.unique_serial || id.serial!=s.device.serial) {
                fail(s,id.status==mlx90614::Status::PecError?Quality::CrcError:Quality::IoError,now);return;
            }
            const auto ambient=mlx90614::temperature(mlx90614::Ambient,mlx90614::read_word(b,a,mlx90614::Ambient));
            if(ambient.status!=mlx90614::Status::Valid) {
                fail(s,ambient.status==mlx90614::Status::PecError?Quality::CrcError:
                    ambient.status==mlx90614::Status::SensorError?Quality::SensorError:Quality::IoError,now);return;
            }
            const auto object=mlx90614::temperature(mlx90614::Object,mlx90614::read_word(b,a,mlx90614::Object));
            if(object.status!=mlx90614::Status::Valid) {
                fail(s,object.status==mlx90614::Status::PecError?Quality::CrcError:
                    object.status==mlx90614::Status::SensorError?Quality::SensorError:Quality::IoError,now);return;
            }
            // Publish both only after both reads pass. These are sequential reads,
            // not a simultaneous physical sample; time denotes read completion.
            s.value.ambient_centi_c=ambient.centi_c;s.value.object_centi_c=object.centi_c;
            sample(s,hw.clock_ms());s.next=hw.clock_ms()+500;
        } else {
            uint8_t status=0xff;
            if(!b.reg(a,0xfb,&status,1)||status>3||!b.reg(a,0,p,12)){fail(s,Quality::IoError,now);return;}
            s.value.io_status=status;
            s.value.mv1=int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);s.value.ua1=int32_t(le32(p+2));
            s.value.mv2=int16_t(uint16_t(p[6])|uint16_t(p[7])<<8);s.value.ua2=int32_t(le32(p+8));
            sample(s,hw.clock_ms());s.phase=1;s.next=now+250;
        }
    }
public:
    static constexpr size_t Capacity=32;
    Slot slots[Capacity]{};
    size_t count=0,cursor=0;
    uint32_t generation=0,epoch=0,max_step_us=0,max_batch_us=0;
    bool enabled=true,overflow=false,route_ok=true;
    void bind(const devices::Inventory& inventory,uint32_t now) {
        count=cursor=0;++epoch;generation=inventory.generation;overflow=false;route_ok=inventory.route_ok;
        if(!inventory.complete || !inventory.route_ok)return;
        for(size_t i=0;i<inventory.count;++i){const auto& d=inventory.entries[i];
            if(d.state!=devices::State::Identified)continue;
            Kind k;
            if(!strcmp(d.driver,"bme680")||!strcmp(d.driver,"bme688"))k=Kind::Env;
            else if(!strcmp(d.driver,"sgp30"))k=Kind::Sgp;
            else if(!strcmp(d.driver,"max30100"))k=Kind::Heart;
            else if(!strcmp(d.driver,"stamplc-io-candidate"))k=Kind::Io;
            else if(!strcmp(d.driver,"mlx90614-candidate") && d.unique_serial)k=Kind::Ncir;
            else if(!strcmp(d.driver,"vl53l1x-candidate") && !d.bus && d.part==0xeacc)k=Kind::Tof;
            else continue;
            if(count==Capacity){overflow=true;continue;}
            slots[count]=Slot{};auto& s=slots[count++];s.device=d;s.kind=k;s.next=now+(k==Kind::Ncir?300:0); // Conservative initial wait; not proof of sensor power-on time.
        }
    }
    // One ready device stage per loop. Unready slots do not hold the bus.
    void tick(Backend& hw) {
        if(!enabled || !count || !route_ok)return;
        uint32_t now=hw.clock_ms();
        for(size_t n=0;n<count;++n){size_t i=cursor++%count;auto& s=slots[i];
            if(!due(now,s.next))continue;
            auto& b=hw.bus(s.device.bus);Slot before=s;
            bool opened=route(b,s.device,true);
            if(opened)step(i,hw,now);
            // Always attempt cleanup, including failed selection/readback.
            bool closed=route(b,s.device,false);
            if(!opened || !closed){s=before;fail(s,Quality::IoError,hw.clock_ms());}
            if(!closed){route_ok=false;for(size_t j=0;j<count;++j)slots[j].quality=Quality::IoError;}
            return;
        }
    }
};
}
