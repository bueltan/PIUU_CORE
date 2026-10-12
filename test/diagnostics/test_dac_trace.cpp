#include "diagnostics/dac_trace.h"
#include <cassert>
#include <cstdio>
int main() {
    Diagnostics::DacTrace trace;Diagnostics::DacTrace::Sample sample{};
    int16_t pcm[]={-32768,32767,0,1};
    trace.begin(9);
    for(unsigned i=0;i<8;++i) trace.record(i,100+i,30,0,8,pcm,4);
    unsigned count=0;
    while(trace.pop(sample)) {
        assert(sample.epoch==9 && sample.sequence==count++);
        assert(sample.peak==32768 && sample.meanAbs==16384 && sample.bytes==8);
    }
    assert(count==5);
    trace.record(20,200,99,-1,4,pcm,4);
    assert(trace.pop(sample) && sample.sequence==20 && sample.error==-1);
    assert(trace.errorCount()==1 && trace.incompleteCount()==1);
    for(unsigned i=0;i<20;++i) { trace.begin(i);trace.record(0,i,1,0,8,pcm,4); }
    assert(trace.droppedCount()==4);
    count=0;while(trace.pop(sample)) ++count;
    assert(count==16);
    trace.begin(30);trace.record(0,300,1,0,8,pcm,4);
    assert(trace.pop(sample) && sample.epoch==30);
    puts("PASS: bounded DAC onset samples, signed PCM levels, failed/partial writes and queue reuse");
}
