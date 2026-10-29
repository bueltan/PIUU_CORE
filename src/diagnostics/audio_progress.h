#pragma once
#include <atomic>
#include <stdint.h>

namespace Diagnostics {
// One playback writer, atomic observations from loop; no lock, heap or formatting.
struct AudioProgress {
    enum Phase : uint32_t { Idle, Poll, Flush, Reset, Jitter, Pop, Decode, Output, Advance, Finished };
    std::atomic<uint32_t> phase{Idle},phaseAt{0},loops{0},loopAt{0},advancedAt{0};
    std::atomic<uint32_t> flags{0},writes{0},writeAt{0},writeUs{0},writeMaxUs{0};
    std::atomic<uint32_t> bytes{0},wanted{0};
    std::atomic<int32_t> error{0};
    std::atomic<uint32_t> endOk{0},endReject{0},lastEnd{0},lastEndEpoch{0};
    void enter(Phase next,uint32_t now) {
        phaseAt.store(now,std::memory_order_relaxed);phase.store(next,std::memory_order_relaxed);
    }
    void tick(uint32_t now) { ++loops;loopAt=now;enter(Poll,now); }
    void advance(uint32_t now) { advancedAt=now; }
    void beginWrite(uint32_t now,uint32_t expectedBytes) {
        wanted=expectedBytes;writeAt=now;enter(Output,now);
    }
    void endWrite(uint32_t durationUs,int32_t err,uint32_t acceptedBytes) {
        writeUs=durationUs;
        if(durationUs>writeMaxUs.load(std::memory_order_relaxed))writeMaxUs=durationUs;
        error=err;bytes=acceptedBytes;++writes;
    }
    // Clamp producer timestamps newer than the reader's now; tolerate rollover.
    static uint32_t age(uint32_t now,uint32_t at) {
        const uint32_t elapsed=now-at;return elapsed<0x80000000u?elapsed:0;
    }
    static const char *name(uint32_t value) {
        switch(value) {
        case Idle:return "idle";case Poll:return "poll";case Flush:return "flush";
        case Reset:return "reset";case Jitter:return "jitter";case Pop:return "pop";
        case Decode:return "decode";case Output:return "output";case Advance:return "advance";
        case Finished:return "finished";
        }
        return "unknown";
    }
};
}
