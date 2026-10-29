#include "network/server_link.h"
#include "hardware/app_log.h"
#include "app/device_volume.h"
#include "network/network_udp.h"
#include "network/session_client.h"
#include "protocol/server_protocol.h"
#include "audio/audio_downlink.h"
#include "app/conversation_event.h"
#include "app/conversation.h"
#include "config/app_config.h"
#include "config/settings.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <atomic>
#include <cstring>
#include "diagnostics/media_rx.h"
#if PIUU_PLAYBACK_DIAGNOSTICS
static Diagnostics::MediaRx mediaRx;
static std::atomic<uint32_t> wireRx{0},badEnvelope{0},clientReject{0},snapshotReject{0};
// Controller-owned captured report. Print at most one line per 100 ms after failure.
static struct {
    SessionProtocol::Identity id{};
    Diagnostics::MediaRx::Sample rejected;
    uint32_t counts[Diagnostics::MediaRx::Count]{};
    uint32_t sequence=0,at=0,lineAt=0,wire=0,envelope=0,client=0,snapshot=0,acceptedSeq=0,acceptedAt=0;
    unsigned line=3;
} playbackFault;
static void pollPlaybackFault();
static uint32_t mediaBaseline[Diagnostics::MediaRx::Count]{},wireBaseline=0,envelopeBaseline=0,clientBaseline=0,snapshotBaseline=0;
#endif
using namespace SessionProtocol;
namespace {
SessionClient client;
ServerEyesSink eyesSink=nullptr;
uint32_t handleEvent(SessionProtocol::Event event,uint32_t response,uint32_t utterance) {
#if PIUU_PLAYBACK_DIAGNOSTICS
    if(event==SessionProtocol::Event::ResponseStarted) {
        for(unsigned i=0;i<Diagnostics::MediaRx::Count;++i) mediaBaseline[i]=mediaRx.count(Diagnostics::MediaRx::Reason(i));
        wireBaseline=wireRx;envelopeBaseline=badEnvelope;clientBaseline=clientReject;snapshotBaseline=snapshotReject;
    }
#endif
    return conversationServerEvent(ConversationEvent(uint8_t(event)),response,utterance);
}
void trace(const char *event,Identity id,uint32_t a,uint32_t b) {
    if(!strncmp(event,"VOLUME_",7)) {
#if PIUU_VOLUME_DIAGNOSTICS
        appLog().printf("[VOLUME_LINK] event=%s c=%08lx%08lx s=%08lx%08lx turn=%lu request=%lu detail=%lu\n",
            event,(unsigned long)(id.client>>32),(unsigned long)id.client,
            (unsigned long)(id.server>>32),(unsigned long)id.server,(unsigned long)id.turn,
            (unsigned long)a,(unsigned long)b);
#endif
        return;
    }
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
        uint8_t mac[6];
        if(esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK) return false;
        client.setObserver(trace);
        if(!client.init(nonce?nonce:1,raw,mac)) return false;
        client.setNotificationHandler(conversationStartNotification);
        client.setStandbyNotificationHandler(conversationAcceptStandbyNotification);
        client.setVolumeHandler(deviceVolumeCommand);
        client.setEventHandler(handleEvent);client.setEventReady(conversationResponseReady);configured=true;publish();
    }
    initialized=true;return true;
}
void serverLinkSetEyesSink(ServerEyesSink sink) { eyesSink=sink; }
Identity serverLinkIdentity() { return client.identity(); }
bool serverLinkEyesPlansEnabled() { return client.eyesPlansEnabled(); }
bool serverLinkAcknowledgeEyesPlan(Identity id,uint32_t sequence) { return client.acknowledgeEyesPlan(id,sequence); }
bool serverLinkSend(const uint8_t *data,size_t size) { return initialized && client.sendBody(data,size,millis()); }
void serverLinkResetHeartbeat() { if(configured) {client.reset();publish();} }
bool serverLinkReachable() { return initialized && client.reachable(millis()); }
bool serverLinkResponsive() { return initialized && client.responsive(millis()); }
bool serverLinkPollHeartbeat(bool online) {
    OwnedControl event{};
    for(int i=0;i<16 && controls && xQueueReceive(controls,&event,0)==pdTRUE;++i)
        if(event.kind==Kind::Opus) client.observeMedia(event.id,event.sequence);
        else client.receive({event.kind,event.flags,event.id,event.sequence,event.body,event.size},millis());
    client.poll(millis(),online && initialized);publish();
#if PIUU_PLAYBACK_DIAGNOSTICS
    pollPlaybackFault();
#endif
    if(online && initialized) audioDownlinkPollRecovery();
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
#if PIUU_PLAYBACK_DIAGNOSTICS
    ++wireRx;
#endif
    if(!decode(packet,size,view)) {
#if PIUU_PLAYBACK_DIAGNOSTICS
        ++badEnvelope;
#endif
        ++invalidRx;return 0;
    }
    const bool haveSnapshot=snapshot(expected);
    if(!haveSnapshot || view.id.client!=expected.client) {
#if PIUU_PLAYBACK_DIAGNOSTICS
        if(haveSnapshot) ++clientReject;else ++snapshotReject;
#endif
        ++staleRx;return 0;
    }
    if(view.kind==Kind::Opus || view.kind==Kind::AudioEnd) {
        const auto reason=Diagnostics::MediaRx::validate(view,expected,capacity);
        if(reason!=Diagnostics::MediaRx::Accepted) {
#if PIUU_PLAYBACK_DIAGNOSTICS
            mediaRx.record(reason,view,expected,millis());
#endif
            ++staleRx;return 0;
        }
        ServerProtocol::OpusPacket inner{};
        if(!ServerProtocol::decodeOpus(view.body,view.size,inner) || inner.sequence!=view.sequence ||
           (view.kind==Kind::AudioEnd && !inner.end) || (view.kind==Kind::Opus && inner.end)) {
#if PIUU_PLAYBACK_DIAGNOSTICS
            mediaRx.record(Diagnostics::MediaRx::Payload,view,expected,millis());
#endif
            ++invalidRx;return 0;
        }
#if PIUU_PLAYBACK_DIAGNOSTICS
        mediaRx.record(Diagnostics::MediaRx::Accepted,view,expected,millis());
#endif
        receivedMedia=view.id;memcpy(data,view.body,view.size);return view.size;
    }
    if(view.kind!=Kind::HelloAck && !sameSession(view.id,expected)) { ++staleRx;return 0; }
    if(view.kind==Kind::Eyes || view.kind==Kind::EyesPlan) {
        if(view.flags || !view.id.server || !sameTurn(view.id,expected) || view.id.response!=expected.response || !view.size || view.size>31) { ++staleRx;return 0; }
        if(view.kind==Kind::EyesPlan && (!view.id.turn || !view.id.response || view.sequence!=1 || view.size>28)) { ++invalidRx;return 0; }
        if(eyesSink) eyesSink(view);
        return 0; // Never use the reliable conversation control queue for visuals.
    }
    if(view.kind==Kind::EyesPlanAck) { ++invalidRx;return 0; } // Server cannot acknowledge its own plan.
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
    appLog().printf("[V3_STATS t=%lu] turn=%lu pcm_tx=%lu tx_fail=%lu nack=%lu retx=%lu cache_miss=%lu cache_expired=%lu nack_age_max_ms=%lu retx_fail=%lu retry=%lu startup_drop=%lu bad_rx=%lu stale_rx=%lu ctrl_drop=%lu\n",
        (unsigned long)millis(),(unsigned long)client.identity().turn,(unsigned long)s.pcmSent,
        (unsigned long)s.txFailed,(unsigned long)s.nacks,(unsigned long)s.retransmits,
        (unsigned long)s.cacheMisses,(unsigned long)s.cacheExpired,(unsigned long)s.nackAgeMaxMs,
        (unsigned long)s.retransmitFailed,(unsigned long)s.retries,(unsigned long)s.overwritten,
        (unsigned long)invalidRx.load(),(unsigned long)staleRx.load(),(unsigned long)controlDrops.load());
#endif
}

bool serverLinkRequestDownlink(uint32_t base,uint16_t mask) { return client.requestDownlink(base,mask); }
void serverLinkAcknowledgeEnd(uint32_t sequence) {
    OwnedControl marker{Kind::AudioEndAck,0,receivedMedia,sequence,0,{}};
    // A dropped ACK is repaired by the server END retry, never a control-overflow abort.
    if(!controls || xQueueSend(controls,&marker,0)!=pdTRUE) ++controlDrops;
}

// Controller captures before conversation reset; formatting is drained by poll.
void serverLinkLogPlaybackFault() {
#if PIUU_PLAYBACK_DIAGNOSTICS
    using R=Diagnostics::MediaRx;
    playbackFault.id=client.identity();playbackFault.rejected=mediaRx.rejected();
    for(unsigned i=0;i<R::Count;++i) playbackFault.counts[i]=mediaRx.count(R::Reason(i))-mediaBaseline[i];
    playbackFault.wire=wireRx.load()-wireBaseline;playbackFault.envelope=badEnvelope.load()-envelopeBaseline;
    playbackFault.client=clientReject.load()-clientBaseline;playbackFault.snapshot=snapshotReject.load()-snapshotBaseline;
    playbackFault.acceptedSeq=mediaRx.lastAcceptedSeq;playbackFault.acceptedAt=mediaRx.lastAcceptedAt;
    uint32_t rejects=0;for(unsigned i=1;i<R::Count;++i) rejects+=playbackFault.counts[i];
    if(!rejects) playbackFault.rejected.valid=false;
    if(!playbackFault.counts[R::Accepted]) playbackFault.acceptedSeq=playbackFault.acceptedAt=0;
    if(++playbackFault.sequence==0) ++playbackFault.sequence;
    playbackFault.at=millis();playbackFault.lineAt=millis();playbackFault.line=0;
#endif
}
#if PIUU_PLAYBACK_DIAGNOSTICS
static void pollPlaybackFault() {
    auto &f=playbackFault;
    if(f.line>=3 || uint32_t(millis()-f.lineAt)<100) return;
    f.lineAt=millis();using R=Diagnostics::MediaRx;
    if(f.line==0) {
        appLog().printf("[MEDIA_PATH t=%lu] fault=%lu turn=%lu resp=%lu wire=%lu envelope=%lu client=%lu snapshot=%lu accepted=%lu flags=%lu session_bad=%lu turn_bad=%lu response_bad=%lu capacity=%lu payload=%lu\n",
            (unsigned long)f.at,(unsigned long)f.sequence,(unsigned long)f.id.turn,(unsigned long)f.id.response,
            (unsigned long)f.wire,(unsigned long)f.envelope,(unsigned long)f.client,(unsigned long)f.snapshot,
            (unsigned long)f.counts[R::Accepted],(unsigned long)f.counts[R::Flags],(unsigned long)f.counts[R::Session],
            (unsigned long)f.counts[R::Turn],(unsigned long)f.counts[R::Response],(unsigned long)f.counts[R::Capacity],(unsigned long)f.counts[R::Payload]);
    } else if(f.line==1) {
        const auto &r=f.rejected;
        appLog().printf("[MEDIA_LAST t=%lu] fault=%lu accept_seq=%lu accept_age_ms=%lu reject_valid=%d reason=%s seq=%lu got_turn=%lu got_resp=%lu want_turn=%lu want_resp=%lu reject_age_ms=%lu\n",
            (unsigned long)f.at,(unsigned long)f.sequence,(unsigned long)f.acceptedSeq,
            (unsigned long)(f.counts[R::Accepted]?uint32_t(f.at-f.acceptedAt):0),r.valid,
            r.valid?R::name(r.reason):"none",(unsigned long)r.seq,(unsigned long)r.turn,(unsigned long)r.response,
            (unsigned long)r.expectedTurn,(unsigned long)r.expectedResponse,(unsigned long)(r.valid?uint32_t(f.at-r.at):0));
    } else networkUdpLogPlaybackFault(f.sequence);
    ++f.line;
}
#endif
