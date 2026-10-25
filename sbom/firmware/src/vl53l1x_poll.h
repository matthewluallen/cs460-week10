// Bounded result polling for an already configured VL53L1X.
// Caller owns initialization, route selection, and device/epoch admission.
#pragma once
#include "vl53l1x_measurement.h"
namespace vl53l1x {
enum class PollState : uint8_t { Idle, Waiting, Complete, Timeout, IoError };
class Poller {
    bool armed_=false;
    uint32_t started_=0, timeout_=0;
public:
    PollState state=PollState::Idle;
    Measurement measurement{};
    uint32_t completed_ms=0;
    bool arm(uint32_t now,uint32_t timeout_ms) {
        measurement=Measurement{};completed_ms=0;armed_=false;state=PollState::Idle;
        // Restrict wrap-safe subtraction to bounded conversion deadlines.
        if(timeout_ms==0 || timeout_ms>60000)return false;
        started_=now;timeout_=timeout_ms;armed_=true;state=PollState::Waiting;return true;
    }
    template<class Bus,class Clock> PollState poll(Bus& bus,uint8_t address,Clock clock) {
        if(!armed_)return state;
        auto expire=[&](){return uint32_t(clock()-started_)>=timeout_;};
        auto fail=[&](PollState s){armed_=false;state=s;measurement=Measurement{};return state;};
        if(expire())return fail(PollState::Timeout);
        uint8_t polarity=0,status=0;
        if(!bus.reg16(address,0x0030,&polarity,1) || !bus.reg16(address,0x0031,&status,1))
            return fail(PollState::IoError);
        if(expire())return fail(PollState::Timeout);
        if((status&1)!=uint8_t(!(polarity&0x10)))return state;
        Measurement candidate=read_result(bus,address);
        if(candidate.quality==Quality::IoError)return fail(PollState::IoError);
        // Clear even a range-error result, so the next poll can obtain new data.
        // Failure to clear never publishes the candidate as a usable sample.
        uint8_t clear[]={0x00,0x86,0x01};
        if(!bus.write(address,clear,sizeof(clear)))return fail(PollState::IoError);
        if(expire())return fail(PollState::Timeout);
        measurement=candidate;completed_ms=clock();armed_=false;state=PollState::Complete;
        return state;
    }
};
} // namespace vl53l1x
