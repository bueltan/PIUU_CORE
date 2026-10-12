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
bool wifiServiceBusy() { return false; }
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
    const auto n=encode(wire,sizeof(wire),{Kind::HelloAck,3,hello.id,hello.sequence,nullptr,0});
    incoming.assign(wire,wire+n);
    uint8_t data[MAX_PACKET];assert(serverLinkParsePacket()==int(n));assert(serverLinkRead(data,sizeof(data))==0);
}
int main() {
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
    uiStatusRefreshStartScreen();assert(drawn==UI_READY);
    const int before=draws;uiStatusRefreshStartScreen();assert(draws==before);
    assert(conversationStart() && conversationState()==ConversationState::Connecting && draws==before);
    uiStatusRefreshStartScreen();assert(drawn==UI_CONNECTING);
    conversationServerEvent(ConversationEvent::Ready,0,0);
    uiStatusRefreshStartScreen();assert(drawn==UI_LISTENING);
    assert(!conversationStart());
    conversationServerEvent(ConversationEvent::SpeechStopped,0,1);
    conversationServerEvent(ConversationEvent::ResponseStarted,1,1);
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
