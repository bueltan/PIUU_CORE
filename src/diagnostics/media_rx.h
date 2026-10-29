#pragma once
#include <atomic>
#include <stdint.h>
#include "protocol/session_protocol.h"

namespace Diagnostics {
// Fixed counters, one RX writer and controller observations. No payload retention.
class MediaRx {
public:
    enum Reason { Accepted, Flags, Session, Turn, Response, Capacity, Payload, Count };
    struct Sample { bool valid=false; uint32_t reason=0,seq=0,turn=0,response=0,expectedTurn=0,expectedResponse=0,at=0; };
    static Reason validate(const SessionProtocol::Packet &p,const SessionProtocol::Identity &expected,size_t capacity) {
        if(p.flags) return Flags;
        if(!SessionProtocol::sameSession(p.id,expected)) return Session;
        if(!expected.turn || p.id.turn!=expected.turn) return Turn;
        if(p.id.response!=expected.response) return Response;
        if(p.size>capacity) return Capacity;
        return Accepted;
    }
    void record(Reason reason,const SessionProtocol::Packet &p,const SessionProtocol::Identity &expected,uint32_t now) {
        ++counts_[reason];
        if(reason==Accepted) { lastAcceptedSeq=p.sequence;lastAcceptedAt=now;return; }
        ++revision_;
        reason_=reason;seq_=p.sequence;turn_=p.id.turn;response_=p.id.response;
        expectedTurn_=expected.turn;expectedResponse_=expected.response;at_=now;
        ++revision_;
    }
    uint32_t count(Reason reason) const { return counts_[reason].load(); }
    Sample rejected() const {
        Sample s;
        for(unsigned i=0;i<2;++i) {
            const auto revision=revision_.load();if(!revision || (revision&1)) continue;
            s.reason=reason_;s.seq=seq_;s.turn=turn_;s.response=response_;
            s.expectedTurn=expectedTurn_;s.expectedResponse=expectedResponse_;s.at=at_;
            if(revision==revision_.load()) { s.valid=true;break; }
        }
        return s;
    }
    static const char *name(uint32_t reason) {
        switch(reason) { case Accepted:return "accepted";case Flags:return "flags";case Session:return "session";case Turn:return "turn";
            case Response:return "response";case Capacity:return "capacity";case Payload:return "payload"; }
        return "none";
    }
    std::atomic<uint32_t> lastAcceptedSeq{0},lastAcceptedAt{0};
private:
    std::atomic<uint32_t> counts_[Count]{};
    std::atomic<uint32_t> revision_{0},reason_{0},seq_{0},turn_{0},response_{0},expectedTurn_{0},expectedResponse_{0},at_{0};
};
}
