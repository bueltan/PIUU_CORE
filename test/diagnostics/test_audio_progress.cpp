#include "diagnostics/audio_progress.h"
#include <cassert>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdio>
int main() {
    using Diagnostics::AudioProgress;
    AudioProgress probe;
    std::mutex mutex;std::condition_variable cv;
    bool entered=false,release=false;
    std::thread worker([&] {
        probe.tick(100);probe.beginWrite(101,640);
        // The observer must work even when the output driver has not returned.
        std::unique_lock<std::mutex> lock(mutex);entered=true;cv.notify_one();
        cv.wait(lock,[&]{return release;});
        probe.endWrite(5000000,-1,20);probe.advance(5101);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return entered;});
    }
    assert(probe.phase==AudioProgress::Output && probe.writes==0 && probe.wanted==640);
    assert(AudioProgress::age(5101,probe.phaseAt)==5000);
    assert(AudioProgress::age(5101,probe.loopAt)==5001);
    {
        std::lock_guard<std::mutex> lock(mutex);release=true;
    }
    cv.notify_one();worker.join();
    assert(probe.writes==1 && probe.writeUs==5000000 && probe.error==-1 && probe.bytes==20);
    probe.beginWrite(6000,640);probe.endWrite(1234,0,640);
    assert(probe.writes==2 && probe.writeUs==1234 && probe.writeMaxUs==5000000);
    assert(probe.error==0 && probe.bytes==640);
    assert(AudioProgress::age(0x10u,0xfffffff0u)==32);
    assert(AudioProgress::age(100,101)==0);
    puts("PASS: blocked output observation, completed/error writes, rollover and concurrent timestamp clamp");
}
