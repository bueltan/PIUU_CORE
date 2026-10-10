#include "app/ears_session.h"
#include "app/conversation.h"
#include "app/conversation_transport.h"
#include "app/app_services.h"
#include "audio/audio_uplink.h"
#include "audio/audio_downlink.h"
#include "input/ears_link.h"
#include "freertos/task.h"
#include <cassert>
#include <deque>
#include <vector>
#include <cstring>
#include <cstdio>
Logger Serial;
static uint32_t now=1000,order=0,epoch=0,drops=0;
static bool peer=false,wifi=true,server=true,ready=true,streaming=false;
static std::deque<EarsMessage> incoming;
static std::vector<bool> commands;
static std::vector<std::vector<uint8_t>> sent;
uint32_t millis() { return now; }
bool appServicesWifiReady() { return wifi; }
bool appServicesServerReady() { return server; }
bool appServicesReady() { return ready; }
void audioDownlinkBeginTurn(uint32_t e) { epoch=e; }
void audioDownlinkExpectResponse() {}
void audioDownlinkLogDiagnostics() {}
bool earsLinkBegin() { return true; }
bool earsLinkPeerOnline() { return peer; }
uint32_t earsLinkReceivedOrder() { return order; }
bool earsLinkRequestStreaming(bool value) { streaming=value;commands.push_back(value);return true; }
EarsLinkStats earsLinkStats() { return {0,0,drops,0,0,streaming}; }
bool earsLinkTake(EarsMessage &message) { if(incoming.empty()) return false;message=incoming.front();incoming.pop_front();return true; }
static bool sender(const uint8_t *p,size_t size) { sent.emplace_back(p,p+size);return true; }
static void pcm(uint32_t seq,int16_t value=123) {
    EarsMessage m{};m.kind=EarsProtocol::Kind::Pcm;m.audio.order=++order;m.audio.sequence=seq;
    for(auto &sample:m.audio.samples) sample=value;
    incoming.push_back(m);
}
static void event(uint8_t code,std::vector<uint8_t> payload={}) {
    EarsMessage m{};m.kind=EarsProtocol::Kind::Event;m.event.order=++order;m.event.code=code;m.event.size=payload.size();
    if(!payload.empty()) memcpy(m.event.payload,payload.data(),payload.size());
    incoming.push_back(m);
}
static uint32_t be32(const uint8_t *p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
static int cancels=0;
static bool beginTransport() { return true; }
static void cancelTransport(uint32_t) { ++cancels; }
static int extras=0;
static void extra(uint8_t code,const uint8_t *p,size_t n) { assert(code==42 && n==3 && p[2]==3);++extras; }
static void finishInput() {
    conversationEndInput();
    for(int i=0;i<10;++i) { now+=20;conversationPoll(); }
    assert(conversationState()==ConversationState::WaitingResponse && streaming);
}
int main() {
    assert(conversationInit() && earsSessionInit());audioUplinkSetSender(sender);
    assert(conversationToggleInput()==ConversationAction::InputUnavailable);
    pcm(99);earsSessionPoll();assert(conversationState()==ConversationState::Idle && sent.empty());
    peer=true;event(1,{17});pcm(100);pcm(102,-321);
    assert(earsSessionPoll()==EarsSessionNotice::Started && streaming && sent.size()==2);
    assert(be32(sent[0].data()+6)==0 && be32(sent[1].data()+6)==2 && be32(sent[1].data()+10)==640);
    int16_t sample=0;memcpy(&sample,sent[1].data()+18,2);assert(sample==-321);
    event(1);pcm(103);event(42,{1,2,3});earsSessionSetExtraEventSink(extra);earsSessionPoll();
    assert(extras==1 && be32(sent.back().data()+6)==3); // repeated wake must not reset input
    const size_t inputPackets=sent.size();event(3);pcm(104);earsSessionPoll();
    assert(conversationState()==ConversationState::ClosingInput && streaming && sent.size()==inputPackets);
    finishInput();assert(sent.back()==std::vector<uint8_t>({'C','O','M','M','I','T'}));
    assert(conversationPostPlaybackEvent({epoch,PlaybackEventKind::Started}));conversationPoll();
    assert(conversationState()==ConversationState::Playing && streaming);
    conversationSetTransportPort({beginTransport,cancelTransport,nullptr,nullptr,nullptr});
    const auto beforeInterrupt=sent.size();event(1);pcm(105);earsSessionPoll();
    assert(conversationState()==ConversationState::Listening && sent.size()==beforeInterrupt+1 && cancels==1);
    finishInput();
    const auto count=sent.size();
    assert(conversationPostPlaybackEvent({epoch,PlaybackEventKind::Finished}));conversationPoll();
    assert(conversationState()==ConversationState::Idle && !streaming);
    // Touch starts explicitly; already queued PCM cannot enter its new turn.
    pcm(200);assert(conversationToggleInput()==ConversationAction::Started && streaming);
    earsSessionPoll();assert(sent.size()==count);
    pcm(201);pcm(201);earsSessionPoll();assert(sent.size()==count+1 && be32(sent.back().data()+6)==0);
    pcm(0);assert(earsSessionPoll()==EarsSessionNotice::Restarted && conversationState()==ConversationState::Idle && !streaming);
    assert(conversationStart());pcm(UINT32_MAX);pcm(0);earsSessionPoll();assert(be32(sent.back().data()+6)==1);
    conversationAbort();wifi=false;event(1);assert(earsSessionPoll()==EarsSessionNotice::Rejected && !streaming);
    wifi=true;server=false;event(1);earsSessionPoll();assert(conversationState()==ConversationState::Idle && !streaming);
    server=true;assert(conversationStart());peer=false;assert(earsSessionPoll()==EarsSessionNotice::Offline && !streaming);
    peer=true;assert(conversationStart());event(1);pcm(1);++drops;
    assert(earsSessionPoll()==EarsSessionNotice::ControlLost && conversationState()==ConversationState::Idle);
    earsSessionPoll();assert(incoming.empty() && conversationState()==ConversationState::Idle);
    now+=1000;conversationPoll();assert(!audioUplinkIsClosing());
    hostStopWorkers();
    puts("PASS: touch/wake start, ordered PCM/STOP, stale input, sequence gaps/rollover, continuous response stream, offline/rejected wake and overflow");
}
