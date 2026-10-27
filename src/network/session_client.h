#pragma once
#include "protocol/session_protocol.h"
#include <array>
#include <memory>
// Controller-owned transport state; the RX worker hands controls through a queue.
class SessionClient {
public:
    using EventHandler=uint32_t(*)(SessionProtocol::Event,uint32_t,uint32_t);
    void setEventHandler(EventHandler handler) { eventHandler=handler; }
    void setEventReady(bool (*ready)()) { eventReady=ready; }
    bool interrupt(uint32_t playedMs);
    void playbackFinished(uint32_t playedMs);
    bool requestDownlink(uint32_t base,uint16_t mask);
    bool eyesPlansEnabled() const { return eyesPlans; }
    bool acknowledgeEyesPlan(SessionProtocol::Identity identity,uint32_t sequence);
    using Sender=bool(*)(const uint8_t*,size_t);
    using Observer=void(*)(const char*,SessionProtocol::Identity,uint32_t,uint32_t);
    struct Stats { uint32_t pcmSent=0,txFailed=0,nacks=0,retransmits=0,cacheMisses=0,retries=0,overwritten=0,retransmitFailed=0,cacheExpired=0,nackAgeMaxMs=0; };
    void setObserver(Observer value) { observer=value; }
    Stats stats() const { return counters; }
    bool init(uint64_t client,Sender sender);
    void reset();
    bool ready(uint32_t now) const;
    bool reachable(uint32_t now) const;
    void observeMedia(SessionProtocol::Identity identity,uint32_t now);
    uint32_t receiveAge(uint32_t now) const { return uint32_t(now-lastReceive); }
    uint32_t pongAge(uint32_t now) const { return uint32_t(now-lastPong); }
    bool beginConversation();
    void cancel(uint32_t playedMs);
    bool sendBody(const uint8_t*,size_t,uint32_t now);
    void receive(const SessionProtocol::Packet&,uint32_t now);
    void poll(uint32_t now,bool online);
    bool takeFailure() { bool v=failed;failed=false;return v; }
    SessionProtocol::Identity identity() const { return id; }
    // Tune with backend recovery window; one startup allocation, no streaming malloc.
    static constexpr size_t CACHE_FRAMES=32;
    static constexpr uint32_t CACHE_TTL_MS=640;
    static constexpr uint32_t CONTROL_RETRY_MS=150;
    static constexpr uint8_t CONTROL_ATTEMPTS=48;
    static constexpr size_t RECOVERY_BUDGET=2;
private:
    struct Command { SessionProtocol::Kind kind;SessionProtocol::Identity id;uint32_t request;uint8_t body[8];uint16_t size;uint8_t attempts;uint32_t sent; };
    struct Frame { uint8_t body[658];uint16_t size;uint32_t sequence,time;bool valid,sent,retried,resend; };
    SessionProtocol::Identity id{};
    Sender sender=nullptr;
    Observer observer=nullptr;
    EventHandler eventHandler=nullptr;
    struct EventAck { uint32_t sequence=0,played=0;SessionProtocol::Identity id{}; };
    EventAck eventAcks[16]{};
    uint32_t lastEvent=0;
    bool (*eventReady)()=nullptr;
    bool awaitingResponseReset=false;
    Stats counters{};
    void reportFor(const char *event,SessionProtocol::Identity identity,uint32_t a=0,uint32_t b=0) {
        if(observer) observer(event,identity,a,b);
    }
    void report(const char *event,uint32_t a=0,uint32_t b=0) { reportFor(event,id,a,b); }
    Command commands[4]{};size_t count=0;
    using FramePool=std::array<Frame,CACHE_FRAMES>;
    std::unique_ptr<FramePool> cache;size_t cursor=0;
    uint32_t nextTurn=0,nextRequest=0,lastHello=0,lastPing=0,lastPong=0,lastReceive=0,helloRequest=0;
    uint32_t helloStarted=0,helloAttempts=0;
    bool eyesPlans=false;
    bool helloSent=false,pingSent=false,established=false,available=false,started=false,failed=false;
    bool emit(SessionProtocol::Kind,SessionProtocol::Identity,uint32_t,const uint8_t*,size_t,uint16_t=0);
    bool queue(SessionProtocol::Kind,const uint8_t*,size_t);
};
