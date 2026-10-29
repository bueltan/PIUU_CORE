#include "audio/downlink_recovery.h"
#include "audio/playback_completion.h"
#include <cassert>
#include <set>
int main() {
    DownlinkRecovery r;DownlinkRecovery::Request q{};std::set<uint32_t> absent{10,11,14};
    auto present=[&](uint32_t seq) { return !absent.count(seq); };
    assert(!r.poll(0,0,50,false,present,q));
    assert(!r.poll(20,0,50,false,present,q));
    assert(r.poll(40,0,50,false,present,q) && q.base==10 && q.mask==19);
    r.arrived(10);r.arrived(10);assert(r.recovered==1);
    assert(!r.poll(60,0,50,false,present,q)); // one request even if NACK lost
    r.reset();assert(!r.poll(0,8,50,true,present,q));
    assert(!r.poll(40,8,50,true,present,q)); // insufficient playback lead
    r.reset();assert(!r.poll(0,0,14,false,present,q));
    assert(r.poll(40,0,14,false,present,q) && q.mask==3); // END exclusive boundary
    r.reset();assert(!r.poll(0,0,50,false,present,q));absent.clear();
    assert(!r.poll(40,0,50,false,present,q)); // reordered originals arrived
    absent={10};r.reset();assert(!r.poll(0xfffffff0u,0,50,false,present,q));
    assert(r.poll(24,0,50,false,present,q)); // millis rollover
    r.reset();assert(r.frames==0 && r.requests==0);

    // Captured failure: 0..33 queued, newer arrivals rejected beyond window.
    // Recovery must see holes after 33, while respecting jitter admission.
    r.reset();auto initial=[](uint32_t seq) { return seq<34; };
    const uint32_t limit=DownlinkRecovery::recoveryLimit(0,246,100);
    assert(limit==101);
    assert(!r.poll(0,0,limit,false,initial,q));
    assert(r.poll(40,0,limit,false,initial,q) && q.base==34);
    assert(DownlinkRecovery::recoveryLimit(200,199,100)==200);
    assert(DownlinkRecovery::recoveryLimit(200,233,100)==234);
    assert(!DownlinkRecovery::shouldResync(0,101,100,true,false,399,0));
    assert(DownlinkRecovery::shouldResync(0,101,100,true,false,400,0));
    assert(!DownlinkRecovery::shouldResync(0,100,100,true,false,500,0));
    assert(!DownlinkRecovery::shouldResync(0,101,100,false,false,500,0));
    assert(!DownlinkRecovery::shouldResync(0,101,100,true,true,500,0));
    assert(!DownlinkRecovery::shouldResync(200,100,100,true,false,500,0));
    assert(!DownlinkRecovery::shouldResync(0,101,100,true,false,99,100));
    assert(DownlinkRecovery::shouldResync(0,101,100,true,false,384,0xfffffff0u));

    // Replay a Wi-Fi burst gap followed by continuous frames through END.
    // The old policy remains at 34 packets and never starts; the new policy
    // reanchors once, fills 50 slots and can consume the remaining response.
    uint32_t expected=0,observed=33,lastActivity=660,high=33;
    std::set<uint32_t> queued;for(unsigned s=0;s<34;++s) queued.insert(s);
    bool started=false;unsigned resyncs=0,decoded=0;
    for(uint32_t seq=130;seq<421;++seq) {
        const uint32_t now=seq*20;
        observed=seq;
        if(seq<=expected+100) { queued.insert(seq);high=seq;lastActivity=now; }
        if(DownlinkRecovery::shouldResync(expected,observed,100,
                !started && queued.size()<50,false,now,lastActivity)) {
            queued.clear();expected=observed;high=observed;r.reset();++resyncs;
        }
        if(!started && queued.size()>=50) { expected=*queued.begin();started=true; }
        if(started) { assert(queued.erase(expected)==1);++expected;++decoded; }
        assert(queued.size()<=170);
    }
    assert(resyncs==1 && started && decoded>0 && high==420);
    // END fits the existing admission window after resynchronization and
    // drains a short tail without needing another 50 buffered frames.
    assert(421-expected<=170);
    while(expected<421) { assert(queued.erase(expected)==1);++expected;++decoded; }
    assert(playbackCompletion(true,expected,421,queued.size(),true,9000,lastActivity,10000)==PlaybackCompletion::Finished);
    // Resetting recovery on reanchor allows repair in the fresh window.
    r.reset();assert(!r.poll(0,130,150,false,[](uint32_t s){return s!=131;},q));
    assert(r.poll(40,130,150,false,[](uint32_t s){return s!=131;},q) && q.base==131);
}
