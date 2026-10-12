#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace Diagnostics {
// Single producer (playback), single consumer (application loop). No serial I/O here.
class DacTrace {
public:
    struct Sample {
        uint32_t epoch,sequence,atMs,writeUs,bytes,expectedBytes;
        int32_t error;
        uint32_t peak,meanAbs;
    };
    void begin(uint32_t value) { epoch=value;remaining=5; }
    void record(uint32_t sequence,uint32_t atMs,uint32_t writeUs,int32_t error,
                size_t bytes,const int16_t *pcm,size_t samples) {
        const size_t expected=samples*sizeof(int16_t);
        if(error) errors.fetch_add(1,std::memory_order_relaxed);
        if(bytes!=expected) incomplete.fetch_add(1,std::memory_order_relaxed);
        if(!remaining && !error && bytes==expected) return;
        if(remaining) --remaining;
        uint32_t peak=0;uint64_t sum=0;
        for(size_t i=0;i<samples;++i) {
            const int32_t value=pcm[i];const uint32_t magnitude=value<0?-value:value;
            if(magnitude>peak) peak=magnitude;
            sum+=magnitude;
        }
        const uint32_t w=written.load(std::memory_order_relaxed);
        if(uint32_t(w-read.load(std::memory_order_acquire))==CAPACITY) {
            dropped.fetch_add(1,std::memory_order_relaxed);return;
        }
        ring[w%CAPACITY]={epoch,sequence,atMs,writeUs,uint32_t(bytes),uint32_t(expected),
                         error,peak,samples?uint32_t(sum/samples):0};
        written.store(w+1,std::memory_order_release);
    }
    bool pop(Sample &sample) {
        const uint32_t r=read.load(std::memory_order_relaxed);
        if(r==written.load(std::memory_order_acquire)) return false;
        sample=ring[r%CAPACITY];read.store(r+1,std::memory_order_release);return true;
    }
    uint32_t errorCount() const { return errors.load(std::memory_order_relaxed); }
    uint32_t incompleteCount() const { return incomplete.load(std::memory_order_relaxed); }
    uint32_t droppedCount() const { return dropped.load(std::memory_order_relaxed); }
private:
    static constexpr uint32_t CAPACITY=16;
    Sample ring[CAPACITY]{};
    std::atomic<uint32_t> written{0},read{0},errors{0},incomplete{0},dropped{0};
    uint32_t epoch=0;unsigned remaining=0;
};
}
