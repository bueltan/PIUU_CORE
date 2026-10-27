#pragma once
#include <stdint.h>
// Estimated consumed samples at 16 kHz. Writer is playback task only.
// Count accepted samples, not packet sequence (which can skip on recovery).
// Interpolation is capped by submitted media: no advance during empty rebuffer.
struct VisualClock {
    uint32_t consumed,submitted,atMs;
    VisualClock(uint32_t c=0,uint32_t s=0,uint32_t at=0):consumed(c),submitted(s),atMs(at) {}
    uint32_t samples(uint32_t now) const {
        const uint32_t queued=submitted-consumed;
        const uint32_t elapsed=now-atMs;
        return consumed+(elapsed>=((queued+15)/16)?queued:elapsed*16);
    }
    void submit(uint32_t count,uint32_t now) {
        consumed=samples(now);atMs=now;submitted+=count;
    }
};
