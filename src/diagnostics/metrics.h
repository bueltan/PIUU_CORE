#pragma once
#include <stdint.h>

namespace Diagnostics {
// Unsigned subtraction also handles one wrap of the 32-bit runtime clock.
inline uint32_t percentTenths(uint32_t elapsed, uint32_t wall) {
    if (!wall) return 0;
    const uint64_t value=uint64_t(elapsed)*1000/wall;
    return value>1000 ? 1000 : uint32_t(value);
}
struct Timing {
    uint32_t count=0, maxUs=0, overBudget=0;
    uint64_t totalUs=0;
    void add(uint32_t us) {
        ++count; totalUs+=us;
        if (us>maxUs) maxUs=us;
        if (us>20000) ++overBudget;
    }
    uint32_t meanUs() const { return count ? uint32_t(totalUs/count) : 0; }
};
struct Rebuffer {
    uint32_t count=0,totalMs=0,maxMs=0,startedAt=0;
    bool active=false;
    void begin(uint32_t now) {
        if (active) return;
        active=true;startedAt=now;++count;
    }
    uint32_t activeMs(uint32_t now) const { return active ? uint32_t(now-startedAt) : 0; }
    void end(uint32_t now) {
        if (!active) return;
        const uint32_t elapsed=activeMs(now);
        totalMs+=elapsed;
        if (elapsed>maxMs) maxMs=elapsed;
        active=false;
    }
};
}
