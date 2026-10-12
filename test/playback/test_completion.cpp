#include "audio/playback_completion.h"
#include <cassert>
#include <cstdio>
int main() {
    const uint32_t timeout=10000;
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
    assert(!playbackCanDrainEnd(10,12,false,0,399,0,400));
    assert(playbackCanDrainEnd(10,12,false,0,400,0,400)); // lost final frames conceal after grace
    assert(playbackCanDrainEnd(10,12,true,10,100,0,400)); // next frame can drain now
    assert(!playbackCanDrainEnd(10,12,true,11,100,0,400)); // don't skip the missing head
    assert(!playbackCanDrainEnd(12,12,false,0,500,0,400)); // empty/completed END
    assert(playbackCanDrainEnd(10,12,false,0,300,UINT32_MAX-100,400));
    puts("PASS: END completion, short gaps, missing END/first audio failure and rollover");
}
