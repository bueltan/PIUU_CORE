#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Diagnostics {
// Jitter mutex owns this state. Arrival gaps describe observations, not final
// loss: late/reordered packets can still fill them. Resume skips are separate.
struct DownlinkSample {
    uint32_t time=0,expected=0,received=0,interval=0,skipped=0;
    bool resume=false,initial=false;
    DownlinkSample()=default;
    DownlinkSample(uint32_t t,uint32_t e,uint32_t r,uint32_t i,uint32_t s,bool res,bool first)
        :time(t),expected(e),received(r),interval(i),skipped(s),resume(res),initial(first) {}
};
struct DownlinkTrace {
    uint32_t arrivals=0,forwardEvents=0,forwardFrames=0,reordered=0;
    uint32_t maxInterval=0,longIntervals=0,skipped=0,sampleDropped=0;
    uint32_t lastTime=0,nextSequence=0;
    bool seen=false;
    DownlinkSample samples[8]{};
    size_t head=0,count=0;
    void add(DownlinkSample sample) {
        if(count==8) { ++sampleDropped;return; }
        samples[(head+count)%8]=sample;++count;
    }
    void arrival(uint32_t sequence,uint32_t now) {
        const uint32_t interval=seen?uint32_t(now-lastTime):0;
        if(interval>maxInterval) maxInterval=interval;
        if(interval>=100) ++longIntervals;
        const uint32_t distance=sequence-nextSequence;
        const bool forward=distance && distance<0x80000000u;
        if(forward) { ++forwardEvents;forwardFrames+=distance; }
        if(seen && distance>=0x80000000u) ++reordered;
        if(forward || interval>=100) add({now,nextSequence,sequence,interval,0,false,false});
        if(!seen || distance<0x80000000u) nextSequence=sequence+1;
        seen=true;lastTime=now;++arrivals;
    }
    void resume(uint32_t expected,uint32_t received,uint32_t now,bool initial) {
        const uint32_t distance=received-expected;
        const uint32_t omitted=distance<0x80000000u?distance:0;
        skipped+=omitted;
        add({now,expected,received,0,omitted,true,initial});
    }
    bool take(DownlinkSample &sample) {
        if(!count)return false;
        sample=samples[head];head=(head+1)%8;--count;return true;
    }
};
}
