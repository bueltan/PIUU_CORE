#include "diagnostics/metrics.h"
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
    puts("Diagnostics metrics: OK");
}
