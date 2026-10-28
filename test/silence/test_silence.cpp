#include "audio/silence_monitor.h"
#include <cassert>
#include <cstdio>
int main() {
    SilenceMonitor silence;int16_t pcm[320]={};silence.reset(100);
    silence.observe(pcm,320,110,160);assert(silence.level()==0 && silence.quietMs(110)==10);
    for(auto &s:pcm) s=159;
    silence.observe(pcm,320,200,160);assert(silence.quietMs(200)==100);
    for(auto &s:pcm) s=-160;
    silence.observe(pcm,320,300,160);assert(silence.level()==160 && silence.quietMs(300)==0);
    for(auto &s:pcm) s=-32768;
    silence.observe(pcm,320,400,160);assert(silence.level()==32768);
    silence.activity(500);assert(silence.quietMs(30499)==29999 && silence.quietMs(30500)==30000);
    silence.reset(UINT32_MAX-10);assert(silence.quietMs(20)==31);
    silence.reset(0);
    for(auto &s:pcm) s=499;
    for(uint32_t t=0;t<=30000;t+=20) silence.observe(pcm,320,t,600,200);
    assert(silence.quietMs(30000)==30000);
    for(auto &s:pcm) s=900;
    silence.observe(pcm,320,30100,600,200);
    silence.observe(pcm,320,30300,600,200); // Sparse peaks do not accumulate.
    assert(silence.quietMs(30300)==30300);
    for(uint32_t t=30320;t<=30500;t+=20) silence.observe(pcm,320,t,600,200);
    assert(silence.quietMs(30500)==0);
    silence.reset(UINT32_MAX-100);
    for(unsigned i=0;i<=10;++i) silence.observe(pcm,320,uint32_t(UINT32_MAX-100+i*20),600,200);
    assert(silence.quietMs(99)==0);
    puts("PASS: quiet/audio threshold, negative full scale, inactivity and millis rollover");
}
