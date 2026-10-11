#include "network/server_link.h"
#include "hardware/app_log.h"
#include "network/network_udp.h"
#include "network/session_client.h"
#include "app/conversation_event.h"
#include "config/app_config.h"
#include "config/settings.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <atomic>
#include <cstring>
using namespace SessionProtocol;
namespace {
SessionClient client;
uint32_t handleEvent(SessionProtocol::Event event,uint32_t response,uint32_t utterance) {
    return conversationServerEvent(ConversationEvent(uint8_t(event)),response,utterance);
}
void trace(const char *event,Identity id,uint32_t a,uint32_t b) {
    if(!strcmp(event,"HELLO_SEND") || !strcmp(event,"HELLO_CONNECTED")) {
#if PIUU_NETWORK_DIAGNOSTICS
        appLog().printf("[CONNECT t=%lu] %s c=%08lx%08lx %s=%lu %s=%lu\n",
            (unsigned long)millis(),event,(unsigned long)(id.client>>32),(unsigned long)id.client,
            !strcmp(event,"HELLO_SEND")?"request":"elapsed_ms",(unsigned long)a,
            !strcmp(event,"HELLO_SEND")?"local_ok":"attempts",(unsigned long)b);
#endif
        return;
    }
#if PIUU_DIAGNOSTICS
    const char *kinds[]={"NONE","HELLO","HELLO_ACK","START","START_ACK","COMMIT","COMMIT_ACK","CANCEL","CANCEL_ACK","PCM","OPUS","NACK","MISSING","ERROR","PING","PONG","EVENT","EVENT_ACK","INTERRUPT","INTERRUPT_ACK","PLAYED","PLAYED_ACK"};
    if(!strncmp(event,"CONTROL",7) || !strncmp(event,"ACK",3)) {
        appLog().printf("[V3 t=%lu] %s c=%08lx%08lx s=%08lx%08lx turn=%lu resp=%lu kind=%s request=%lu\n",
            (unsigned long)millis(),event,(unsigned long)(id.client>>32),(unsigned long)id.client,
            (unsigned long)(id.server>>32),(unsigned long)id.server,(unsigned long)id.turn,
            (unsigned long)id.response,a<22?kinds[a]:"UNKNOWN",(unsigned long)b);
        return;
    }
    appLog().printf("[V3 t=%lu] %s c=%08lx%08lx s=%08lx%08lx turn=%lu resp=%lu a=%lu b=%lu\n",
        (unsigned long)millis(),event,(unsigned long)(id.client>>32),(unsigned long)id.client,
        (unsigned long)(id.server>>32),(unsigned long)id.server,(unsigned long)id.turn,
        (unsigned long)id.response,(unsigned long)a,(unsigned long)b);
#endif
}
std::atomic<uint32_t> invalidRx{0},staleRx{0},controlDrops{0};
bool initialized=false,configured=false;
QueueHandle_t controls=nullptr;
struct OwnedControl { Kind kind;uint16_t flags;Identity id;uint32_t sequence;uint16_t size;uint8_t body[64]; };
std::atomic<uint32_t> revision{0},clientHi{0},clientLo{0},serverHi{0},serverLo{0},turn{0},response{0};
std::atomic_bool overflow{false};
Identity receivedMedia{}; // RX worker only
uint32_t lastMediaNotice=0;
Identity lastMediaIdentity{};
bool raw(const uint8_t *data,size_t size) { return networkUdpSendRaw(settings().server.c_str(),SERVER_PORT,data,size); }
void publish() {
    const auto id=client.identity();++revision;
    clientHi=uint32_t(id.client>>32);clientLo=uint32_t(id.client);
    serverHi=uint32_t(id.server>>32);serverLo=uint32_t(id.server);turn=id.turn;response=id.response;++revision;
}
bool snapshot(Identity &id) {
    for(int i=0;i<2;++i) {
        const uint32_t before=revision.load();if(before&1) continue;
        id={uint64_t(clientHi.load())<<32|clientLo.load(),uint64_t(serverHi.load())<<32|serverLo.load(),turn.load(),response.load()};
        if(before==revision.load()) return true;
    }
    return false;
}
}
bool serverLinkInit() {
    if(initialized) return true;
    if(!controls) controls=xQueueCreate(16,sizeof(OwnedControl));
    if(!controls || !networkUdpInit(LOCAL_UDP_PORT)) return false;
    if(!configured) {
        uint64_t nonce=uint64_t(esp_random())<<32|esp_random();
        client.init(nonce?nonce:1,raw);client.setObserver(trace);client.setEventHandler(handleEvent);client.setEventReady(conversationResponseReady);configured=true;publish();
    }
    initialized=true;return true;
}
bool serverLinkSend(const uint8_t *data,size_t size) { return initialized && client.sendBody(data,size,millis()); }
void serverLinkResetHeartbeat() { if(configured) {client.reset();publish();} }
bool serverLinkReachable() { return initialized && client.reachable(millis()); }
bool serverLinkPollHeartbeat(bool online) {
    OwnedControl event{};
    for(int i=0;i<16 && controls && xQueueReceive(controls,&event,0)==pdTRUE;++i)
        if(event.kind==Kind::Opus) client.observeMedia(event.id,event.sequence);
        else client.receive({event.kind,event.flags,event.id,event.sequence,event.body,event.size},millis());
    client.poll(millis(),online && initialized);publish();
    return online && initialized && client.ready(millis());
}
bool serverLinkOpenConversation() { const bool ok=client.beginConversation();publish();return ok; }
void serverLinkPlaybackFinished(uint32_t playedMs) { client.playbackFinished(playedMs);publish(); }
bool serverLinkInterrupt(uint32_t playedMs) { const bool ok=client.interrupt(playedMs);publish();return ok; }
void serverLinkCloseConversation(uint32_t playedMs) { client.cancel(playedMs);publish(); }
bool serverLinkTakeFailure() {
    const bool queuedFailure=overflow.exchange(false);
    const bool sessionFailure=client.takeFailure();
    if(queuedFailure) trace("CONTROL_RX_OVERFLOW",client.identity(),0,0);
    return queuedFailure || sessionFailure;
}
int serverLinkParsePacket() { return networkUdpParsePacket(); }
int serverLinkRead(uint8_t *data,size_t capacity) {
    uint8_t packet[MAX_PACKET];const int size=networkUdpRead(packet,sizeof(packet));
    Packet view{};Identity expected{};
    if(size<=0) return 0;
    if(!decode(packet,size,view)) { ++invalidRx;return 0; }
    if(!snapshot(expected) || view.id.client!=expected.client) { ++staleRx;return 0; }
    if(view.kind==Kind::Opus) {
        if(view.flags || !expected.turn || !sameTurn(view.id,expected) || view.id.response!=expected.response || view.size>capacity) { ++staleRx;return 0; }
        receivedMedia=view.id;memcpy(data,view.body,view.size);return view.size;
    }
    if(view.kind!=Kind::HelloAck && !sameSession(view.id,expected)) { ++staleRx;return 0; }
    if(view.size>64) return 0;
    OwnedControl event{view.kind,view.flags,view.id,view.sequence,view.size,{}};
    if(view.size) memcpy(event.body,view.body,view.size);
    if(!controls || xQueueSend(controls,&event,0)!=pdTRUE) { ++controlDrops;overflow.store(true); }
    return 0;
}

void serverLinkObserveMedia() {
    const uint32_t now=millis();
    if(sameTurn(receivedMedia,lastMediaIdentity) && receivedMedia.response==lastMediaIdentity.response && uint32_t(now-lastMediaNotice)<1000) return;
    OwnedControl marker{Kind::Opus,0,receivedMedia,now,0,{}};
    // Media never competes indefinitely with reliable controls; retry on next frame.
    if(controls && xQueueSend(controls,&marker,0)==pdTRUE) { lastMediaNotice=now;lastMediaIdentity=receivedMedia; }
}

// Controller calls periodically. Counters are cumulative across turns/reconnects.
void serverLinkLogDiagnostics() {
    networkUdpLogDiagnostics();
#if PIUU_DIAGNOSTICS
    const auto s=client.stats();
    appLog().printf("[LINK_STATS t=%lu] reachable=%d admission=%d rx_age_ms=%lu pong_age_ms=%lu\n",
        (unsigned long)millis(),serverLinkReachable(),client.ready(millis()),
        (unsigned long)client.receiveAge(millis()),(unsigned long)client.pongAge(millis()));
    appLog().printf("[V3_STATS t=%lu] turn=%lu pcm_tx=%lu tx_fail=%lu nack=%lu retx=%lu cache_miss=%lu retry=%lu startup_drop=%lu bad_rx=%lu stale_rx=%lu ctrl_drop=%lu\n",
        (unsigned long)millis(),(unsigned long)client.identity().turn,(unsigned long)s.pcmSent,
        (unsigned long)s.txFailed,(unsigned long)s.nacks,(unsigned long)s.retransmits,
        (unsigned long)s.cacheMisses,(unsigned long)s.retries,(unsigned long)s.overwritten,
        (unsigned long)invalidRx.load(),(unsigned long)staleRx.load(),(unsigned long)controlDrops.load());
#endif
}
