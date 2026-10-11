#pragma once
#include <stdint.h>
enum class PlaybackCompletion { Pending, Finished, Stalled };
inline PlaybackCompletion playbackCompletion(bool end,uint32_t expected,uint32_t finalSequence,
    unsigned buffered,bool waiting,uint32_t now,uint32_t lastActivity,uint32_t timeout,bool blockedOnBuffer=false) {
    if(end && !buffered && expected>=finalSequence) return PlaybackCompletion::Finished;
    if(waiting && (!buffered || blockedOnBuffer) && uint32_t(now-lastActivity)>=timeout) return PlaybackCompletion::Stalled;
    return PlaybackCompletion::Pending;
}
