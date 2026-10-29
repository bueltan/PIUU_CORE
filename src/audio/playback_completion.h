#pragma once
#include <stdint.h>
#include "audio/playback_timing.h"
enum class PlaybackCompletion { Pending, Finished, Stalled };
inline PlaybackCompletion playbackCompletion(bool end,uint32_t expected,uint32_t finalSequence,
    unsigned buffered,bool waiting,uint32_t now,uint32_t lastActivity,uint32_t timeout,bool blockedOnBuffer=false) {
    if(end && !buffered && expected>=finalSequence) return PlaybackCompletion::Finished;
    if(waiting && (!buffered || blockedOnBuffer) && PlaybackTiming::elapsed(now,lastActivity)>=timeout) return PlaybackCompletion::Stalled;
    return PlaybackCompletion::Pending;
}

// END is an exclusive boundary. Wait briefly for tail recovery before concealment;
// an absent END never authorizes a successful completion.
inline bool playbackCanDrainEnd(uint32_t expected,uint32_t finalSequence,bool found,
    uint32_t minimum,uint32_t now,uint32_t endAt,uint32_t grace) {
    return expected<finalSequence && ((found && minimum==expected) || PlaybackTiming::elapsed(now,endAt)>=grace);
}
