#pragma once
#include <stdint.h>
#include <stddef.h>
// Metadata only: PCM/Opus remain in the existing jitter slots. Owner polls under
// jitterMutex, then sends outside it. One request per sequence, bounded scan/work.
class DownlinkRecovery {
public:
    static constexpr uint32_t WINDOW=128, REORDER_MS=40, POLL_MS=20;
    static constexpr uint32_t MIN_LEAD_FRAMES=8, END_GRACE_MS=400;
    struct Request { uint32_t base;uint16_t mask; };
    void reset() { for(auto &s:slots) s.valid=false;polled=false;requests=frames=recovered=0; }
    template<class Present>
    bool poll(uint32_t now,uint32_t expected,uint32_t limit,bool advancing,Present present,Request &out) {
        if(polled && uint32_t(now-lastPoll)<POLL_MS) return false;
        polled=true;lastPoll=now;out={0,0};
        const uint32_t count=limit>expected ? (limit-expected<WINDOW?limit-expected:WINDOW):0;
        for(uint32_t offset=0;offset<count;++offset) {
            const uint32_t seq=expected+offset;
            auto &s=slots[seq%WINDOW];
            if(present(seq)) continue;
            if(!s.valid || s.seq!=seq) { s={seq,now,true,false,false};continue; }
            if(s.sent || uint32_t(now-s.first)<REORDER_MS || (advancing && offset<MIN_LEAD_FRAMES)) continue;
            if(!out.mask) out.base=seq;
            if(seq-out.base>=8) break;
            out.mask|=uint16_t(1u<<(seq-out.base));s.sent=true;++frames;
        }
        if(out.mask) { ++requests;return true; }
        return false;
    }
    void arrived(uint32_t seq) {
        auto &s=slots[seq%WINDOW];
        if(s.valid && s.seq==seq && s.sent && !s.received) { s.received=true;++recovered; }
    }
    uint32_t requests=0,frames=0,recovered=0;
private:
    struct Slot { uint32_t seq,first;bool valid,sent,received; } slots[WINDOW]{};
    uint32_t lastPoll=0;bool polled=false;
};
