#include "app/ears_session.h"
#include "app/conversation.h"
#include "app/conversation_event.h"
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
bool audioDownlinkEpochReady() { return true; }
void audioDownlinkBeginTurn(uint32_t e) { epoch=e; }
void audioDownlinkExpectResponse() {}
void audioDownlinkLogDiagnostics() {}
bool earsLinkBegin() { return true; }
bool earsLinkPeerOnline() { return peer; }
uint32_t earsLinkReceivedOrder() { return order; }
bool earsLinkRequestStreaming(bool value) { streaming=value;commands.push_back(value);return true; }
EarsLinkStats earsLinkStats() { return {0,0,drops,0,0,streaming}; }
bool earsLinkTake(EarsMessage &message) { if(incoming.empty()) return false;message=incoming.front();incoming.pop_front();return true; }
#if PIUU_UART_DIAGNOSTICS
static unsigned gapSamples=0;
void earsLinkRecordConsumerGap(uint32_t expected,uint32_t received,uint32_t) {
    assert(received!=expected);++gapSamples;
}
#endif
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
static bool interruptTransport(uint32_t) { ++cancels;return true; }
static int extras=0;
static void extra(uint8_t code,const uint8_t *p,size_t n) { assert(code==42 && n==3 && p[2]==3);++extras; }
static void finishInput() {
    conversationServerEvent(ConversationEvent::SpeechStopped,0,1);
    conversationServerEvent(ConversationEvent::ResponseStarted,1,1);
    assert(conversationState()==ConversationState::WaitingResponse && streaming);
}
int main() {
    assert(conversationInit() && earsSessionInit());audioUplinkSetSender(sender);
    assert(conversationToggleInput()==ConversationAction::InputUnavailable);
    pcm(99);earsSessionPoll();assert(conversationState()==ConversationState::Idle && sent.empty());
    peer=true;event(1,{17});pcm(100);pcm(102,-321);
    assert(earsSessionPoll()==EarsSessionNotice::Started && streaming && sent.size()==2);
#if PIUU_UART_DIAGNOSTICS
    assert(gapSamples==1);
#endif
    conversationServerEvent(ConversationEvent::Ready,0,0);
    assert(be32(sent[0].data()+6)==0 && be32(sent[1].data()+6)==2 && be32(sent[1].data()+10)==640);
    int16_t sample=0;memcpy(&sample,sent[1].data()+18,2);assert(sample==-321);
    event(1);pcm(103);event(42,{1,2,3});earsSessionSetExtraEventSink(extra);earsSessionPoll();
    assert(extras==1 && be32(sent.back().data()+6)==3); // repeated wake must not reset input
    finishInput();
    assert(conversationInputMuted());
    assert(conversationPostPlaybackEvent({epoch-1,PlaybackEventKind::Finished}));conversationPoll();
    assert(conversationInputMuted()); // Stale completion cannot unmute a new reply.
    assert(conversationPostPlaybackEvent({epoch,PlaybackEventKind::Started}));conversationPoll();
    assert(conversationState()==ConversationState::Playing && streaming);
    const auto whilePlaying=sent.size();pcm(104);earsSessionPoll();assert(sent.size()==whilePlaying);
    conversationSetTransportPort({beginTransport,cancelTransport,nullptr,nullptr,interruptTransport,nullptr});
    const auto beforeInterrupt=sent.size();event(1);pcm(105);earsSessionPoll();
    assert(conversationState()==ConversationState::Playing && sent.size()==beforeInterrupt && cancels==0);
    assert(conversationInputMuted());
    finishInput();
    assert(conversationPostPlaybackEvent({epoch,PlaybackEventKind::Finished}));conversationPoll();
    assert(conversationState()==ConversationState::Listening && streaming);
    assert(conversationInputMuted());
    pcm(106);earsSessionPoll();assert(sent.size()==beforeInterrupt);
    now+=251;conversationPoll();assert(!conversationInputMuted());
    pcm(107);earsSessionPoll();assert(sent.size()==beforeInterrupt+1);
    assert(be32(sent.back().data()+6)==4); // Muting introduces no artificial UDP sequence gap.
    now+=29000;pcm(108,321);earsSessionPoll();
    now+=2000;conversationPoll();assert(conversationState()==ConversationState::Listening && streaming);
    now+=30001;conversationPoll();assert(conversationState()==ConversationState::Idle && !streaming);
    const auto count=sent.size();
    // Touch starts explicitly; already queued PCM cannot enter its new turn.
    pcm(200);assert(conversationToggleInput()==ConversationAction::Started && streaming);
    conversationServerEvent(ConversationEvent::Ready,0,0);
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
    now+=1000;conversationPoll();assert(conversationState()==ConversationState::Idle);
#if PIUU_UART_DIAGNOSTICS
    now+=340;earsSessionPoll();
    assert(earsSessionStats().pollGapMaxMs>=340);
#endif
    hostStopWorkers();
    puts("PASS: touch/wake start, ordered PCM/STOP, stale input, sequence gaps/rollover, half-duplex PCM/wake guard and resume, offline/rejected wake and overflow");
}
