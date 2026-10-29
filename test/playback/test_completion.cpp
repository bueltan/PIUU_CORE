#include "audio/playback_completion.h"
#include <cassert>
#include <cstdio>
int main() {
    const uint32_t timeout=10000;
    // Logged failures: RX timestamp advances 1-2 ms beyond a stale clock read
    // while the first packets are still below the prebuffer threshold.
    assert(playbackCompletion(false,0,0,27,true,1000,1001,timeout,true)==PlaybackCompletion::Pending);
    assert(playbackCompletion(false,0,0,8,true,1000,1002,timeout,true)==PlaybackCompletion::Pending);
    // Same ordering race before the first packet and during rebuffering.
    assert(playbackCompletion(false,0,0,0,true,1000,1002,timeout,true)==PlaybackCompletion::Pending);
    assert(playbackCompletion(false,343,0,5,true,1000,1002,timeout,true)==PlaybackCompletion::Pending);
    assert(playbackCompletion(false,0,0,8,true,UINT32_MAX,0,timeout,true)==PlaybackCompletion::Pending);
    // The observed 1.8-second pause must not finish a response without END.
    assert(playbackCompletion(false,343,0,0,true,2000,0,timeout)==PlaybackCompletion::Pending);
    assert(playbackCompletion(false,343,0,40,true,5000,4900,timeout)==PlaybackCompletion::Pending);
    assert(playbackCompletion(true,343,344,1,true,5000,4900,timeout)==PlaybackCompletion::Pending);
    assert(playbackCompletion(true,344,344,0,true,5000,4900,timeout)==PlaybackCompletion::Finished);
    assert(playbackCompletion(false,343,0,0,true,10000,0,timeout)==PlaybackCompletion::Stalled);
    // A short tail below the rebuffer threshold must not wait forever for END.
    assert(playbackCompletion(false,343,0,5,true,10000,0,timeout,true)==PlaybackCompletion::Stalled);
    assert(playbackCompletion(false,343,0,50,true,10000,0,timeout,false)==PlaybackCompletion::Pending);
    // Lost END / missing final frames fail explicitly, never claim success.
    assert(playbackCompletion(true,343,344,0,true,10000,0,timeout)==PlaybackCompletion::Stalled);
    assert(playbackCompletion(false,0,0,0,true,10000,0,timeout)==PlaybackCompletion::Stalled);
    assert(playbackCompletion(false,0,0,0,false,10000,0,timeout)==PlaybackCompletion::Pending);
    assert(playbackCompletion(false,343,0,0,true,50,UINT32_MAX-100,timeout)==PlaybackCompletion::Pending);
    // Preserve actual timeout boundaries, including across millis() rollover.
    assert(playbackCompletion(false,0,0,8,true,10999,1000,timeout,true)==PlaybackCompletion::Pending);
    assert(playbackCompletion(false,0,0,8,true,11000,1000,timeout,true)==PlaybackCompletion::Stalled);
    const uint32_t beforeWrap=UINT32_MAX-100;
    assert(playbackCompletion(false,0,0,8,true,beforeWrap+9999u,beforeWrap,timeout,true)==PlaybackCompletion::Pending);
    assert(playbackCompletion(false,0,0,8,true,beforeWrap+10000u,beforeWrap,timeout,true)==PlaybackCompletion::Stalled);
    // A newer END timestamp must not bypass the trailing-recovery grace.
    assert(!playbackCanDrainEnd(10,12,false,0,1000,1002,400));
    assert(!playbackCanDrainEnd(10,12,true,11,1000,1002,400));
    assert(!playbackCanDrainEnd(10,12,false,0,UINT32_MAX,0,400));
    assert(playbackCanDrainEnd(10,12,true,10,1000,1002,400)); // available head drains immediately
    assert(!playbackCanDrainEnd(10,12,false,0,1401,1002,400));
    assert(playbackCanDrainEnd(10,12,false,0,1402,1002,400));
    assert(!playbackCanDrainEnd(10,12,false,0,beforeWrap+399u,beforeWrap,400));
    assert(playbackCanDrainEnd(10,12,false,0,beforeWrap+400u,beforeWrap,400));
    assert(!playbackCanDrainEnd(10,12,false,0,399,0,400));
    assert(playbackCanDrainEnd(10,12,false,0,400,0,400)); // lost final frames conceal after grace
    assert(playbackCanDrainEnd(10,12,true,10,100,0,400)); // next frame can drain now
    assert(!playbackCanDrainEnd(10,12,true,11,100,0,400)); // don't skip the missing head
    assert(!playbackCanDrainEnd(12,12,false,0,500,0,400)); // empty/completed END
    assert(playbackCanDrainEnd(10,12,false,0,300,UINT32_MAX-100,400));
    puts("PASS: END completion, concurrent newer timestamps, real timeout/grace boundaries and rollover");
}
