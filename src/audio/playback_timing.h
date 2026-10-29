#pragma once
#include <stdint.h>

namespace PlaybackTiming {
// Playback deadlines are shorter than half a uint32_t clock cycle (~24.9 days).
// Preserve normal millis() rollover; a concurrently newer reference has age 0.
inline uint32_t elapsed(uint32_t now,uint32_t reference) {
    const uint32_t delta=now-reference;
    return delta<0x80000000u?delta:0;
}
}
