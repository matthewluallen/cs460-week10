// VL53L1X initialization. Default register table and configuration values adapted
// from M5Stack M5Unit-TOF ffeb3c9178d05abb9cf9850ab2635381584ee14d.
// Copyright (c) 2024 M5Stack Technology CO LTD. SPDX-License-Identifier: MIT
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace vl53l1x {
class Initializer {
    enum Phase { Identity, ResetLow, ResetHigh, Boot, Defaults, StartCalibration,
                 CalibrationReady, FinishCalibration, Configure, Oscillator, Period, Start, Done, Failed };
    Phase phase_=Identity;
    uint32_t began_=0,next_=0,period_=0;
    unsigned index_=0;
    bool active_=false;
    static constexpr uint8_t defaults_[]={
    0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a, 0x21, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0xc8, 0x00, 0x00, 0x38, 0xff, 0x01, 0x00, 0x08, 0x00, 0x00, 0x01, 0xcc, 0x0f, 0x01, 0xf1, 0x0d, 0x01, 0x68,
    0x00, 0x80, 0x08, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0f,
    0x0d, 0x0e, 0x0e, 0x00, 0x00, 0x02, 0xc7, 0xff, 0x9B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    struct Setting {uint16_t reg,value;uint8_t width;};
    // Long mode, 100 ms timing budget; no offset/crosstalk calibration inferred.
    static constexpr Setting settings_[]={
        {0x004b,0x0a,1},{0x0060,0x0f,1},{0x0063,0x0d,1},
        {0x0069,0xb8,1},{0x0078,0x0f0d,2},{0x007a,0x0e0e,2},
        {0x005e,0x01cc,2},{0x0061,0x01ea,2}
    };
    template<class Bus> static bool write(Bus& b,uint8_t a,uint16_t reg,uint32_t v,unsigned width){
        uint8_t bytes[6]={uint8_t(reg>>8),uint8_t(reg)};
        for(unsigned i=0;i<width;++i)bytes[2+i]=v>>(8*(width-1-i));
        return b.write(a,bytes,width+2);
    }
public:
    enum class Status { Waiting, Ready, IoError, WrongModel, Timeout, InvalidOscillator };
    Status status=Status::Waiting;
    void begin(uint32_t now){phase_=Identity;began_=next_=now;index_=0;period_=0;active_=true;status=Status::Waiting;}
    // Caller selects/releases verified route each tick. At most two transactions,
    // no sleeps. Re-enter begin explicitly following any failure.
    template<class Bus,class Clock> Status tick(Bus& b,uint8_t a,Clock clock){
        if(!active_)return status;
        auto fail=[&](Status s){phase_=Failed;active_=false;return status=s;};
        uint32_t now=clock();
        if(uint32_t(now-began_)>=10000)return fail(Status::Timeout);
        if(int32_t(now-next_)<0)return status;
        uint8_t bytes[2]{};bool ok=true;
        switch(phase_){
        case Identity:
            if(!b.reg16(a,0x010f,bytes,2))return fail(Status::IoError);
            if(bytes[0]!=0xea || bytes[1]!=0xcc)return fail(Status::WrongModel);
            phase_=ResetLow;break;
        case ResetLow:ok=write(b,a,0,0,1);phase_=ResetHigh;next_=clock()+2;break;
        case ResetHigh:ok=write(b,a,0,1,1);phase_=Boot;next_=clock()+2;break;
        case Boot:
            ok=b.reg16(a,0x00e5,bytes,1);
            if(bytes[0]){phase_=Defaults;index_=0;}else next_=clock()+5;
            break;
        case Defaults:
            ok=write(b,a,0x002d+index_,index_==1?1:defaults_[index_],1);
            if(++index_==sizeof(defaults_))phase_=StartCalibration;
            break;
        case StartCalibration:ok=write(b,a,0x0087,0x40,1);phase_=CalibrationReady;break;
        case CalibrationReady:
            ok=b.reg16(a,0x0030,bytes,1)&&b.reg16(a,0x0031,bytes+1,1);
            if((bytes[1]&1)==uint8_t(!(bytes[0]&0x10))){phase_=FinishCalibration;index_=0;}
            else next_=clock()+5;
            break;
        case FinishCalibration: {
            static constexpr Setting finish[]={{0x86,1,1},{0x87,0x80,1},{8,9,1},{0x0b,0,1}};
            auto v=finish[index_];ok=write(b,a,v.reg,v.value,v.width);
            if(++index_==4){phase_=Configure;index_=0;}break;}
        case Configure:{auto v=settings_[index_];ok=write(b,a,v.reg,v.value,v.width);
            if(++index_==sizeof(settings_)/sizeof(settings_[0]))phase_=Oscillator;break;}
        case Oscillator: {
            if(!b.reg16(a,0x00de,bytes,2))return fail(Status::IoError);
            uint16_t osc=((uint16_t(bytes[0])<<8)|bytes[1])&0x3ff;
            if(!osc)return fail(Status::InvalidOscillator);
            // Manufacturer 1.065 oscillator scale, 120 ms between 100 ms budgets.
            period_=uint32_t(osc)*120*1065/1000;phase_=Period;break;}
        case Period:ok=write(b,a,0x006c,period_,4);phase_=Start;break;
        case Start:ok=write(b,a,0x0087,0x40,1);phase_=Done;break;
        default:break;
        }
        if(!ok)return fail(Status::IoError);
        if(uint32_t(clock()-began_)>=10000)return fail(Status::Timeout);
        if(phase_==Done){active_=false;status=Status::Ready;}
        return status;
    }
};
} // namespace vl53l1x
