#include "diagnostics/media_rx.h"
#include <cassert>
#include <cstring>
#include <thread>
#include <atomic>
#include <cstdio>
using namespace SessionProtocol;
int main() {
    using R=Diagnostics::MediaRx;
    R trace;Identity wanted{11,22,33,44};
    Packet p{Kind::Opus,0,wanted,7,nullptr,50};
    assert(R::validate(p,wanted,50)==R::Accepted);
    assert(!trace.rejected().valid && trace.count(R::Accepted)==0);
    trace.record(R::Accepted,p,wanted,100);
    p.flags=1;assert(R::validate(p,wanted,50)==R::Flags);p.flags=0;
    ++p.id.server;assert(R::validate(p,wanted,50)==R::Session);p.id=wanted;
    ++p.id.turn;assert(R::validate(p,wanted,50)==R::Turn);p.id=wanted;
    ++p.id.response;assert(R::validate(p,wanted,50)==R::Response);
    trace.record(R::Response,p,wanted,200);
    auto rejected=trace.rejected();
    assert(rejected.valid && rejected.response==45 && rejected.expectedResponse==44 && rejected.seq==7);
    p.id=wanted;assert(R::validate(p,wanted,49)==R::Capacity);
    assert(!strcmp(R::name(R::Payload),"payload"));
    assert(trace.count(R::Accepted)==1 && trace.count(R::Response)==1);
    // A new accepted frame must not overwrite the last rejection evidence.
    p.sequence=8;trace.record(R::Accepted,p,wanted,300);
    assert(trace.rejected().at==200 && trace.lastAcceptedSeq==8 && trace.lastAcceptedAt==300);
    // RX updates and observer snapshots must not mix two rejection identities.
    std::atomic<bool> done{false};
    std::thread writer([&]{
        for(unsigned i=1;i<20000;++i) {
            Identity expected{11,22,i,i+10};
            Packet bad{Kind::Opus,0,{11,22,i+1,i+11},i,nullptr,50};
            trace.record(R::Turn,bad,expected,i);
        }
        done=true;
    });
    while(!done) {
        auto s=trace.rejected();
        if(s.valid && s.reason==R::Turn) {
            assert(s.seq==s.expectedTurn && s.turn==s.expectedTurn+1);
            assert(s.expectedResponse==s.expectedTurn+10 && s.response==s.expectedResponse+1);
        }
    }
    writer.join();
    assert(trace.count(R::Turn)==19999);
    puts("PASS: media admission reasons, retained rejection and concurrent coherent identity samples");
}
