#include "audio/downlink_recovery.h"
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
}
