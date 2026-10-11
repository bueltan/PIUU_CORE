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
    puts("PASS: END completion, short gaps, missing END/first audio failure and rollover");
}
