#include "network/session_client.h"
#include <cstring>
#include <new>
using namespace SessionProtocol;
namespace {
uint32_t read32(const uint8_t *p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
void put32(uint8_t *p,uint32_t v) { for(int i=3;i>=0;--i) {p[i]=uint8_t(v);v>>=8;} }
Kind ack(Kind k) { return k==Kind::Start?Kind::StartAck:k==Kind::Interrupt?Kind::InterruptAck:k==Kind::Played?Kind::PlayedAck:Kind::CancelAck; }
}
bool SessionClient::init(uint64_t client,Sender send,const uint8_t *mac) {
    hasDeviceMac=mac!=nullptr;
    if(mac) {
        uint8_t bits=0;for(unsigned i=0;i<6;++i) bits|=mac[i];
        if(!bits || (mac[0]&1)) return false;
        memcpy(deviceMac,mac,sizeof(deviceMac));
    }
    if(!cache) cache.reset(new(std::nothrow) FramePool{});
    id={client,0,0,0};sender=send;reset();id.client=client?client:1;
    if(!cache) { failed=true;report("CACHE_ALLOC_FAILED",uint32_t(sizeof(FramePool)));return false; }
    report("CACHE_READY",uint32_t(sizeof(FramePool)),CACHE_FRAMES);return true;
}
void SessionClient::reset() {
    report("RESET");
    // Reconnection/config changes invalidate the old server input even if CANCEL
    // was lost. HELLO with the new incarnation makes the peer clear that state.
    if(++id.client==0) ++id.client;
    id.server=0;id.turn=id.response=0;count=0;established=available=started=helloSent=pingSent=false;
    lastPong=lastReceive=0;awaitingResponseReset=false;if(cache) for(auto &f:*cache) f.valid=false;
    helloAttempts=0;eyesPlans=false;
}
bool SessionClient::reachable(uint32_t now) const { return established && uint32_t(now-lastReceive)<6000; }
bool SessionClient::ready(uint32_t now) const { return reachable(now) && available; }
void SessionClient::observeMedia(Identity identity,uint32_t now) {
    if(id.turn && id.response && sameTurn(identity,id) && identity.response==id.response) lastReceive=now;
}
bool SessionClient::emit(Kind kind,Identity identity,uint32_t seq,const uint8_t *body,size_t size,uint16_t flags) {
    uint8_t packet[MAX_PACKET];
    const size_t bytes=encode(packet,sizeof(packet),{kind,flags,identity,seq,body,uint16_t(size)});
    const bool ok=bytes && sender && sender(packet,bytes);
    if(!ok) ++counters.txFailed;
    else if(kind==Kind::Pcm && size>18) ++counters.pcmSent;
    return ok;
}
bool SessionClient::queue(Kind kind,const uint8_t *body,size_t size) {
    if(count==4 || size>8) { failed=true;return false; }
    if(++nextRequest==0) ++nextRequest;
    Command command{kind,id,nextRequest,{},uint16_t(size),0,0};
    if(size) memcpy(command.body,body,size);
    commands[count++]=command;report("CONTROL_QUEUE",uint32_t(kind),command.request);return true;
}
bool SessionClient::beginConversation() {
    if(!cache || !established || !available || id.turn) return false;
    if(++nextTurn==0) { reset();failed=true;return false; }
    id.turn=nextTurn;id.response=0;lastEvent=0;awaitingResponseReset=false;
    for(auto &a:eventAcks) a.sequence=0;
    started=false;cursor=0;
    for(auto &f:*cache) f.valid=false;
    return queue(Kind::Start,nullptr,0);
}
bool SessionClient::interrupt(uint32_t playedMs) {
    if(!id.turn || !started) return false;
    uint8_t body[4];put32(body,playedMs);
    return queue(Kind::Interrupt,body,4);
}
bool SessionClient::requestDownlink(uint32_t base,uint16_t mask) {
    if(!id.turn || !id.response || !mask || (mask&0xff00)) return false;
    uint8_t body[6];put32(body,base);body[4]=uint8_t(mask>>8);body[5]=uint8_t(mask);
    return emit(Kind::DownlinkNack,id,base,body,6);
}
bool SessionClient::acknowledgeEyesPlan(Identity identity,uint32_t sequence) {
    if(!eyesPlans || !id.turn || !id.response || !sameTurn(id,identity) || id.response!=identity.response || sequence!=1)return false;
    return emit(Kind::EyesPlanAck,id,sequence,nullptr,0);
}
void SessionClient::playbackFinished(uint32_t playedMs) {
    if(!id.turn || !id.response) return;
    uint8_t body[4];put32(body,playedMs);queue(Kind::Played,body,4);
    id.response=0;
}
void SessionClient::cancel(uint32_t playedMs) {
    if(!id.turn) return;
    report("CANCEL",playedMs);
    // A cancel supersedes unacknowledged START/COMMIT. Earlier cancels survive.
    size_t keep=0;for(size_t i=0;i<count;++i) if(commands[i].kind==Kind::Cancel) commands[keep++]=commands[i];
    count=keep;
    uint8_t body[4];put32(body,playedMs);queue(Kind::Cancel,body,4);
    id.turn=id.response=0;started=false;awaitingResponseReset=false;if(cache) for(auto &f:*cache) f.valid=false;
}
bool SessionClient::sendBody(const uint8_t *body,size_t size,uint32_t now) {
    if(!cache || !id.turn || !body) return false;
    if(size<18 || size>658 || memcmp(body,"PCM!",4)) return false;
    const uint32_t seq=read32(body+6);
    if(body[5]&1) return false; // v3 uses explicit conversation close, never PCM END/COMMIT
    Frame &frame=(*cache)[cursor++%CACHE_FRAMES];
    if(frame.valid && !frame.sent) ++counters.overwritten;
    memcpy(frame.body,body,size);frame.size=uint16_t(size);frame.sequence=seq;frame.time=now;
    frame.valid=true;frame.sent=started;frame.retried=frame.resend=false;
    return !started || emit(Kind::Pcm,{id.client,id.server,id.turn,0},seq,body,size);
}
void SessionClient::receive(const Packet &packet,uint32_t now) {
    if(packet.id.client!=id.client) return;
    if(packet.kind==Kind::HelloAck && packet.id.server && packet.sequence==helloRequest && packet.id.turn==0 && packet.id.response==0 && packet.size==0 && !(packet.flags&~uint16_t(3|CAP_EYES_PLAN|CAP_DEVICE_MAC))) {
        if(!(packet.flags&2) || (hasDeviceMac && !(packet.flags&CAP_DEVICE_MAC))) { report("INCOMPATIBLE_SERVER",packet.flags);reset();failed=true;return; }
        if(established && id.server!=packet.id.server) { reset();failed=true;return; }
        const bool changed=!established || available!=bool(packet.flags&1);
        if(!established) report("HELLO_CONNECTED",uint32_t(now-helloStarted),helloAttempts);
        eyesPlans=(packet.flags&CAP_EYES_PLAN)!=0;
        id.server=packet.id.server;established=true;available=(packet.flags&1)!=0;lastPong=lastReceive=now;
        if(changed) report("HELLO_READY",available);
        return;
    }
    if(!sameSession(packet.id,id)) return;
    if(packet.kind==Kind::Pong && packet.id.turn==0 && packet.id.response==0 && packet.size==0 && !(packet.flags&~1u)) {
        const bool changed=available!=bool(packet.flags&1);
        available=(packet.flags&1)!=0;lastPong=lastReceive=now;
        if(changed) report("PONG_READY",available);
        return;
    }
    // RX queues this only after jitter accepted the authoritative END boundary.
    if(packet.kind==Kind::AudioEndAck && sameTurn(packet.id,id) && id.response &&
       packet.id.response==id.response && !packet.flags && !packet.size) {
        lastReceive=now;emit(Kind::AudioEndAck,packet.id,packet.sequence,nullptr,0);
        report("OUTPUT_END_ACK",packet.sequence);return;
    }
    if(packet.kind==Kind::Error) {
        if(!packet.id.turn || sameTurn(packet.id,id)) { report("REMOTE_ERROR",packet.sequence);reset();failed=true; }
        return;
    }
    if(packet.kind==Kind::Event && sameTurn(packet.id,id) && id.turn && packet.flags==0 && packet.size==5) {
        for(const auto &a:eventAcks) if(a.sequence==packet.sequence && sameTurn(a.id,packet.id) && a.id.response==packet.id.response) {
            lastReceive=now;
            if(awaitingResponseReset && a.sequence==lastEvent) return;
            uint8_t body[4];put32(body,a.played);emit(Kind::EventAck,a.id,a.sequence,body,4);return;
        }
        if(packet.sequence!=lastEvent+1) { report("EVENT_OUT_OF_ORDER",packet.sequence,lastEvent);return; }
        const auto event=SessionProtocol::Event(packet.body[0]);
        if(packet.body[0]<1 || packet.body[0]>5 || (event==SessionProtocol::Event::ResponseStarted && !packet.id.response)) return;
        lastReceive=now;
        const uint32_t played=eventHandler ? eventHandler(event,packet.id.response,read32(packet.body+1)) : 0;
        if(event==SessionProtocol::Event::ResponseStarted) id.response=packet.id.response;
        else if(event==SessionProtocol::Event::SpeechStarted) id.response=0;
        lastEvent=packet.sequence;auto &saved=eventAcks[lastEvent%16];saved.sequence=lastEvent;saved.played=played;saved.id=packet.id;
        awaitingResponseReset=event==SessionProtocol::Event::ResponseStarted && eventReady && !eventReady();
        if(!awaitingResponseReset) {uint8_t body[4];put32(body,played);emit(Kind::EventAck,packet.id,lastEvent,body,4);}
        report("SERVER_EVENT",packet.body[0],packet.id.response);return;
    }
    if(count && packet.kind==ack(commands[0].kind) && packet.sequence==commands[0].request &&
       sameTurn(packet.id,commands[0].id) && packet.id.response==commands[0].id.response && packet.size==0 && !(packet.flags&~1u)) {
        lastReceive=now;
        const Kind kind=commands[0].kind;
        reportFor("CONTROL_ACK",packet.id,uint32_t(packet.kind),packet.sequence);
        if(!(packet.flags&1)) { reportFor("ACK_REJECTED",packet.id,uint32_t(packet.kind),packet.sequence);reset();failed=true;return; }
        for(size_t i=1;i<count;++i) commands[i-1]=commands[i];
        --count;
        if(kind==Kind::Start && sameTurn(packet.id,id)) started=true;
        return;
    }
    if(packet.kind!=Kind::Nack || !sameTurn(packet.id,id) || packet.size!=6 || packet.flags || packet.id.response || !started) return;
    const uint16_t requestMask=uint16_t(packet.body[4])<<8|packet.body[5];
    if(!requestMask || (requestMask&0xff00)) return;
    lastReceive=now;
    ++counters.nacks;
    const uint32_t base=read32(packet.body);const uint16_t mask=uint16_t(packet.body[4])<<8|packet.body[5];
    for(uint32_t bit=0;bit<8;++bit) if(mask&(1u<<bit)) {
        const uint32_t seq=base+bit;Frame *found=nullptr;
        for(auto &f:*cache) if(f.valid && f.sequence==seq) {
            const uint32_t age=uint32_t(now-f.time);
            if(age>counters.nackAgeMaxMs) counters.nackAgeMaxMs=age;
            if(age<=CACHE_TTL_MS) found=&f;
            else ++counters.cacheExpired;
            break;
        }
        if(found && !found->retried) found->resend=true;
        else if(!found) { ++counters.cacheMisses;emit(Kind::Missing,{id.client,id.server,id.turn,0},seq,nullptr,0); }
    }
}
void SessionClient::poll(uint32_t now,bool online) {
    if(!online || !sender) return;
    if(!helloSent || uint32_t(now-lastHello)>=(established?3000u:500u)) {
        if(++nextRequest==0) ++nextRequest;
        helloRequest=nextRequest;
        const bool ok=emit(Kind::Hello,{id.client,0,0,0},helloRequest,
            hasDeviceMac?deviceMac:nullptr,hasDeviceMac?sizeof(deviceMac):0,
            CAP_EYES_PLAN|(hasDeviceMac?CAP_DEVICE_MAC:0));
        if(!established) {
            if(!helloAttempts) helloStarted=now;
            ++helloAttempts;
            report("HELLO_SEND",helloRequest,ok);
        }
        lastHello=now;helloSent=true;
    }
    if(!established) return;
    if(awaitingResponseReset && eventReady && eventReady()) {
        const auto &saved=eventAcks[lastEvent%16];uint8_t body[4];put32(body,saved.played);
        emit(Kind::EventAck,saved.id,saved.sequence,body,4);awaitingResponseReset=false;
    }
    if(uint32_t(now-lastReceive)>=6000) { report("HEARTBEAT_TIMEOUT",uint32_t(now-lastReceive),uint32_t(now-lastPong));reset();failed=true;return; }
    if(!pingSent || uint32_t(now-lastPing)>=3000) {
        emit(Kind::Ping,{id.client,id.server,0,0},0,nullptr,0);lastPing=now;pingSent=true;
    }
    if(count) {
        Command &c=commands[0];
        if(!c.attempts || uint32_t(now-c.sent)>=CONTROL_RETRY_MS) {
            if(c.attempts>=CONTROL_ATTEMPTS) { reportFor("ACK_TIMEOUT",c.id,uint32_t(c.kind),c.request);reset();failed=true;return; }
            if(c.attempts) { ++counters.retries;reportFor("CONTROL_RETRY",c.id,uint32_t(c.kind),c.request); }
            emit(c.kind,c.id,c.request,c.body,c.size);c.sent=now;++c.attempts;
        }
    }
    if(started) {
        size_t budget=RECOVERY_BUDGET;
        // Recovery has a deadline: service requested frames before the startup backlog.
        // Each pass retains sequence order and shares the same per-poll send budget.
        for(unsigned pass=0;pass<2 && budget;++pass)
        for(size_t i=0;i<CACHE_FRAMES && budget;++i) {
            Frame &f=(*cache)[(cursor+i)%CACHE_FRAMES];
            if(!f.valid || (pass==0 ? !f.resend : f.sent)) continue;
            const bool ok=emit(Kind::Pcm,{id.client,id.server,id.turn,0},f.sequence,f.body,f.size);
            // Retain capture age and allow only one retransmission attempt, even on local failure.
            f.sent=true;
            if(f.resend) {
                if(ok) ++counters.retransmits; else ++counters.retransmitFailed;
                f.retried=true;f.resend=false;
            }--budget;
        }
    }
}
