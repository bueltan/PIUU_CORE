#include "diagnostics/metrics.h"
#include "diagnostics/downlink_trace.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace Diagnostics;
    assert(percentTenths(250000,1000000)==250);
    assert(percentTenths(0,0)==0);
    assert(percentTenths(110,100)==1000);
    const uint32_t before=0xfffffff0,after=0x10;
    assert(percentTenths(after-before,64)==500);
    Timing timing;
    assert(timing.meanUs()==0);
    timing.add(0);timing.add(20000);timing.add(20001);
    assert(timing.count==3 && timing.maxUs==20001 && timing.overBudget==1);
    assert(timing.meanUs()==13333);
    for (unsigned i=0;i<100000;++i) timing.add(100000);
    assert(timing.totalUs>UINT32_MAX); // Long totals must not overflow 32 bits.
    Rebuffer pauses;
    pauses.end(50);assert(pauses.count==0);
    pauses.begin(before);pauses.begin(0); // Repeated begin cannot restart the clock.
    assert(pauses.activeMs(after)==32 && pauses.count==1);
    pauses.end(after);assert(pauses.totalMs==32 && pauses.maxMs==32);
    assert(pauses.activeMs(123)==0);
    pauses.begin(100);pauses.end(200);
    assert(pauses.count==2 && pauses.totalMs==132 && pauses.maxMs==100);
    DownlinkTrace trace;
    trace.arrival(0,UINT32_MAX-9);trace.arrival(1,10);
    assert(trace.maxInterval==20 && trace.forwardFrames==0);
    trace.arrival(4,130); // Two observed holes; reordered frames may fill them.
    trace.arrival(2,131);trace.arrival(3,132);trace.arrival(4,133);
    assert(trace.forwardFrames==2 && trace.reordered==3 && trace.nextSequence==5);
    assert(trace.longIntervals==1 && trace.skipped==0);
    trace.resume(5,157,200,false);
    assert(trace.skipped==152); // Resume losses must not disappear behind miss attempts.
    DownlinkSample sample;
    assert(trace.take(sample) && !sample.resume && sample.expected==2 && sample.interval==120);
    assert(trace.take(sample) && sample.resume && sample.skipped==152);
    assert(!trace.take(sample));
    for(unsigned i=0;i<20;++i)trace.resume(i,i,300+i,false);
    assert(trace.count==8 && trace.sampleDropped==12);
    trace=DownlinkTrace{};trace.arrival(UINT32_MAX,0);trace.arrival(0,20);
    assert(trace.nextSequence==1 && trace.reordered==0); // Sequence wrap.
    puts("Diagnostics metrics: OK");
}
