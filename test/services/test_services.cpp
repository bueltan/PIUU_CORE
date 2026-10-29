#include "app/app_services.h"
#include "config/settings.h"
#include "ui/ui_model.h"
#include "audio/audio_downlink.h"

#include "app/conversation.h"
#include "app/conversation_event.h"
#include "audio/audio_uplink.h"
#include "network/server_link.h"
#include "protocol/session_protocol.h"
#include "ui/ui_status.h"
#include "ui/screens/ui_start.h"
#include <cassert>
#include "freertos/task.h"
#include <cstring>
#include <vector>
#include <cstdio>
Logger Serial;
static uint32_t now=1000;
uint32_t millis() { return now; }
static DeviceSettings config;
const DeviceSettings &settings() { return config; }
static uint32_t epoch=0;
static PlaybackEventSink sink=nullptr;
void audioDownlinkSetEventSink(PlaybackEventSink value) { sink=value; }
bool audioDownlinkEpochReady() { return true; }
void audioDownlinkBeginTurn(uint32_t value) { epoch=value; }
void audioDownlinkExpectResponse() {}
void audioDownlinkLogDiagnostics() {}
static bool wifi=false,bindOk=false,audioOk=false,tasksOk=false;
static int binds=0,inits=0,starts=0,draws=0,wifiDraws=0;
static UiState drawn=UI_READY;
static std::vector<std::vector<uint8_t>> sent;
static std::vector<uint8_t> incoming;
bool wifiServiceConnected() { return wifi; }
static bool wifiBusy=false;
bool wifiServiceBusy() { return wifiBusy; }
bool networkUdpInit(uint16_t) { ++binds; return bindOk; }
void networkUdpLogDiagnostics() {}
bool networkUdpSendRaw(const char *,uint16_t,const uint8_t *p,size_t n) { sent.emplace_back(p,p+n); return true; }
int networkUdpParsePacket() { return incoming.size(); }
int networkUdpRead(uint8_t *p,size_t n) {
    assert(n>=incoming.size());const int size=incoming.size();
    memcpy(p,incoming.data(),incoming.size());incoming.clear();return size;
}
bool audioDownlinkInit() { ++inits;return audioOk; }
bool audioDownlinkStartTasks() { ++starts;return tasksOk; }

void uiStartApplyState(UiState state) { drawn=state;++draws; }
void uiStartDrawWifiStatus() { ++wifiDraws; }
static void pong() {
    using namespace SessionProtocol;
    Packet hello{};
    for(auto it=sent.rbegin();it!=sent.rend();++it) {
        if(decode(it->data(),it->size(),hello) && hello.kind==Kind::Hello) break;
    }
    uint8_t wire[MAX_PACKET];hello.id.server=42;
    const auto n=encode(wire,sizeof(wire),{Kind::HelloAck,7|CAP_DEVICE_MAC,hello.id,hello.sequence,nullptr,0});
    incoming.assign(wire,wire+n);
    uint8_t data[MAX_PACKET];assert(serverLinkParsePacket()==int(n));assert(serverLinkRead(data,sizeof(data))==0);
}
static unsigned eyePackets=0,scheduledPackets=0;
static void eyeSink(const SessionProtocol::Packet& p) {
    if(p.kind==SessionProtocol::Kind::EyesPlan) { assert(p.size==12);++scheduledPackets;return; }
    assert(p.size==9 && !memcmp(p.body,"color red",9));++eyePackets;
}
int main() {
    serverLinkSetEyesSink(eyeSink);
    assert(conversationInit()); appServicesInit();appServicesRefreshConnectivity();appServicesPoll();
    assert(!appServicesReady() && !appServicesWifiReady() && binds==0 && draws==0);
    uiStatusRefreshStartScreen();assert(drawn==UI_ERROR && draws==1);
    uiStatusRefreshStartScreen();assert(draws==1); // unchanged presentation does no SPI work
    wifi=true;appServicesRefreshConnectivity();assert(appServicesWifiReady() && !appServicesReady() && binds==1);
    bindOk=true;appServicesRefreshConnectivity();assert(!appServicesReady() && binds==2 && inits==1);
    audioOk=true;appServicesRefreshConnectivity();assert(!appServicesReady() && binds==2 && starts==1);
    tasksOk=true;appServicesRefreshConnectivity();assert(appServicesReady() && starts==2);
    const int stableInits=inits;
    for(int i=0;i<5;++i) { appServicesRefreshConnectivity();assert(appServicesStartIfNeeded()); }
    assert(inits==stableInits && starts==2 && draws==1);
    appServicesPoll();SessionProtocol::Packet hello{};assert(SessionProtocol::decode(sent.back().data(),sent.back().size(),hello) && hello.kind==SessionProtocol::Kind::Hello);
    assert(!appServicesServerReady());pong();assert(!appServicesServerReady());
    appServicesPoll();assert(appServicesServerReady());
    { // Visual bursts do not consume the reliable control queue.
        using namespace SessionProtocol;
        uint8_t wire[MAX_PACKET],out[MAX_PACKET];const auto id=serverLinkIdentity();
        const auto sendVisual=[&](Identity identity,uint16_t flags) {
            const auto n=encode(wire,sizeof(wire),{Kind::Eyes,flags,identity,1,reinterpret_cast<const uint8_t*>("color red"),9});
            incoming.assign(wire,wire+n);assert(serverLinkRead(out,sizeof(out))==0);
        };
        for(unsigned i=0;i<100;++i)sendVisual(id,0);
        assert(eyePackets==100 && !serverLinkTakeFailure());
        auto stale=id;++stale.response;sendVisual(stale,0);
        stale=id;++stale.server;sendVisual(stale,0);sendVisual(id,1);
        assert(eyePackets==100);
    }
    uiStatusRefreshStartScreen();assert(drawn==UI_READY);
    const int before=draws;uiStatusRefreshStartScreen();assert(draws==before);
    assert(conversationStart() && conversationState()==ConversationState::Connecting && draws==before);
    uiStatusRefreshStartScreen();assert(drawn==UI_CONNECTING);
    conversationServerEvent(ConversationEvent::Ready,0,0);
    uiStatusRefreshStartScreen();assert(drawn==UI_LISTENING);
    // A pending scan/job must not simulate WiFi loss, reset UDP identity or
    // terminate an active conversation while the driver remains connected.
    const auto scanIdentity=serverLinkIdentity();
    wifiBusy=true;appServicesPoll();appServicesRefreshConnectivity();conversationPoll();
    assert(appServicesWifiReady() && appServicesServerReady());
    assert(serverLinkIdentity().client==scanIdentity.client);
    assert(conversationState()==ConversationState::Listening);
    wifiBusy=false;appServicesPoll();
    assert(serverLinkIdentity().client==scanIdentity.client);
    assert(!conversationStart());
    conversationServerEvent(ConversationEvent::SpeechStopped,0,1);
    {
        using namespace SessionProtocol;
        assert(serverLinkOpenConversation()); // This UI fixture has no transport port installed.
        auto identity=serverLinkIdentity();identity.response=1;
        uint8_t wire[MAX_PACKET],out[MAX_PACKET];const uint8_t event[]={3,0,0,0,1};
        auto size=encode(wire,sizeof(wire),{Kind::Event,0,identity,1,event,5});
        incoming.assign(wire,wire+size);serverLinkRead(out,sizeof(out));appServicesPoll();
        assert(serverLinkIdentity().response==1 && serverLinkEyesPlansEnabled());
        const uint8_t plan[]={1,1,0,0,1,7,4,0xb0,0,0,0,0};
        const auto deliverPlan=[&](Kind kind,Identity id,uint32_t seq) {
            size=encode(wire,sizeof(wire),{kind,0,id,seq,plan,sizeof(plan)});
            incoming.assign(wire,wire+size);assert(!serverLinkRead(out,sizeof(out)));
        };
        for(unsigned i=0;i<100;++i)deliverPlan(Kind::EyesPlan,identity,1);
        assert(scheduledPackets==100 && !serverLinkTakeFailure());
        auto stale=identity;++stale.response;deliverPlan(Kind::EyesPlan,stale,1);
        deliverPlan(Kind::EyesPlan,identity,2);deliverPlan(Kind::EyesPlanAck,identity,1);
        assert(scheduledPackets==100 && !serverLinkTakeFailure());
        assert(serverLinkAcknowledgeEyesPlan(identity,1));
        Packet ack{};assert(decode(sent.back().data(),sent.back().size(),ack) && ack.kind==Kind::EyesPlanAck && !ack.size);
        serverLinkCloseConversation(0);
    }
    assert(sink({epoch,PlaybackEventKind::Started}));conversationPoll();uiStatusRefreshStartScreen();assert(drawn==UI_SPEAKING);
    assert(sink({epoch,PlaybackEventKind::Finished}));conversationPoll();assert(conversationState()==ConversationState::Listening);
    uiStatusHandleConversationToggle();assert(conversationState()==ConversationState::Idle && drawn==UI_READY);
    uiModel().screen=SCREEN_WIFI;const int hidden=draws;uiStatusRefreshStartScreen();assert(draws==hidden);
    uiModel().screen=SCREEN_START;uiStatusInvalidate();uiStatusRefreshStartScreen();assert(drawn==UI_READY);
    // A closing backend is reachable but busy, not "No server".
    SessionProtocol::Packet lastHello{};
    for(auto it=sent.rbegin();it!=sent.rend();++it) {
        if(SessionProtocol::decode(it->data(),it->size(),lastHello) && lastHello.kind==SessionProtocol::Kind::Hello) break;
    }
    uint8_t busyWire[SessionProtocol::MAX_PACKET];lastHello.id.server=42;
    auto busySize=SessionProtocol::encode(busyWire,sizeof busyWire,{SessionProtocol::Kind::Pong,0,lastHello.id,0,nullptr,0});
    incoming.assign(busyWire,busyWire+busySize);uint8_t busyData[64];serverLinkRead(busyData,sizeof busyData);
    appServicesPoll();assert(appServicesServerReachable()&&!appServicesServerReady());
    uiStatusRefreshStartScreen();assert(uiModel().errorLine1=="Server busy");
    pong();appServicesPoll();uiStatusRefreshStartScreen();assert(drawn==UI_READY);
    now+=6001;appServicesPoll();assert(!appServicesServerReady());
    wifi=false;appServicesRefreshConnectivity();assert(!appServicesWifiReady() && appServicesReady());
    wifi=true;appServicesRefreshConnectivity();appServicesPoll();assert(!appServicesServerReady() && starts==2);
    pong();appServicesPoll();assert(appServicesServerReady());
    // Naked v1 and unknown datagrams cannot bypass v2.
    incoming={'P','O','N','G','!'};uint8_t data[32];assert(serverLinkRead(data,sizeof(data))==0);
    // Unsigned heartbeat age survives millis wraparound.
    now=UINT32_MAX-100;pong();now=50;appServicesPoll();assert(appServicesServerReady());
    assert(conversationStart());
    const uint32_t oldEpoch=epoch;
    wifi=false;appServicesPoll();conversationPoll();assert(conversationState()==ConversationState::Idle);
    assert(sink({oldEpoch,PlaybackEventKind::Started}));conversationPoll();assert(conversationState()==ConversationState::Idle);
    wifi=true;appServicesRefreshConnectivity();appServicesPoll();pong();appServicesPoll();
    uiStatusSetError("Old failure","Retry");uiStatusRefreshStartScreen();assert(drawn==UI_ERROR);
    uiStatusHandleConversationToggle();assert(conversationState()==ConversationState::Connecting && drawn==UI_CONNECTING);
    conversationServerEvent(ConversationEvent::Ready,0,0);
    conversationServerEvent(ConversationEvent::ResponseStarted,1,1);
    assert(sink({epoch,PlaybackEventKind::Failed}));conversationPoll();assert(conversationState()==ConversationState::Idle);
    conversationAbort();
    hostStopWorkers();
    puts("PASS: startup retry/idempotence, heartbeat/reconnect/rollover, UI isolation and conversation actions");
}
