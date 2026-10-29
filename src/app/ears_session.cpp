#include "app/ears_session.h"
#include "hardware/app_log.h"
#include "app/conversation.h"
#include "app/conversation_input.h"
#include "app/conversation_transport.h"
#include "input/ears_link.h"
#include "audio/audio_uplink.h"

namespace {
uint32_t inputAfter=0,lastSequence=0,controlDrops=0;
bool haveSequence=false,wakePending=false;
bool keywordsEnabled=true;
uint32_t wakeOrder=0,discardThrough=0;
bool discardQueued=false;
EarsExtraEventSink extraSink=nullptr;
EarsSessionStats stats{};
EarsSessionNotice notice=EarsSessionNotice::None;
bool start() {
    if (!earsLinkPeerOnline()) return false;
    inputAfter=wakePending ? wakeOrder : earsLinkReceivedOrder();
    haveSequence=false;
    return earsLinkRequestStreaming(true);
}
void stop() { haveSequence=false;earsLinkRequestStreaming(false); }
void syncKeywords() {
    const bool enabled=!conversationKeywordsMuted();
    if(enabled!=keywordsEnabled && earsLinkRequestKeywords(enabled)) {
        keywordsEnabled=enabled;
        appLog().printf("[KEYWORD_GATE] enabled=%d state=%u\n",enabled,unsigned(conversationState()));
    }
}
void handleEvent(const EarsEvent &event) {
#if PIUU_DIAGNOSTICS
    appLog().printf("[EARS_EVT t=%lu] code=%u order=%lu size=%u state=%u\n",
        (unsigned long)millis(),unsigned(event.code),(unsigned long)event.order,unsigned(event.size),unsigned(conversationState()));
#endif
    // Reject detections already in flight when speaker playback/echo began.
    if(conversationKeywordsMuted()) return;
    if (event.code==uint8_t(EarsProtocol::Event::Wake)) {
        if(conversationInputMuted() && !conversationInputStandby()) return; // Speaker audio must not wake/interrupt PIUU.
        if ((conversationState()==ConversationState::Listening && !conversationInputStandby()) || conversationState()==ConversationState::Connecting) return; // Idempotent repeat, preserve turn sequence.

        wakePending=true;wakeOrder=event.order;
        const bool started=conversationInputStandby() ? conversationResumeInput() :
            (conversationState()==ConversationState::Idle ? conversationStart() : conversationInterrupt());wakePending=false;
        if (!started) stop(); // EARS enabled streaming itself on wake; reject explicitly.
        notice=started ? EarsSessionNotice::Started : EarsSessionNotice::Rejected;
    } else if (event.code==uint8_t(EarsProtocol::Event::Stop)) {
        conversationClose();
    } else {
        ++stats.unknownEvents;
        if (extraSink) extraSink(event.code,event.payload,event.size);
    }
}
void handleAudio(const EarsAudioFrame &frame) {
    if (conversationState()==ConversationState::Idle || conversationInputStandby() || int32_t(frame.order-inputAfter)<=0) return;
    if (haveSequence) {
        const uint32_t delta=frame.sequence-lastSequence;
        if (!delta) { ++stats.duplicates;return; }
        if (delta>=0x80000000u || delta>251) {
            // UART preserves order. A backwards sequence means EARS restarted
            // or the stream is corrupt; never advance the server by billions.
            conversationAbort(ConversationCloseReason::EarsRestarted);notice=EarsSessionNotice::Restarted;return;
        }
        if (delta>1) {
            earsLinkRecordConsumerGap(lastSequence+1,frame.sequence,frame.order);
            stats.sequenceGaps+=delta-1;if(!conversationInputMuted()) audioUplinkSkipFrames(delta-1); }
    }
    haveSequence=true;lastSequence=frame.sequence;
    if(conversationInputMuted()) return; // Drain UART, retain sequence tracking, send no captured audio.
    conversationObserveAudio(frame.samples,PCM_FRAME_SAMPLES);
    if (audioUplinkSubmitFrame(frame.samples,PCM_FRAME_SAMPLES)) ++stats.forwarded;
}
}
bool earsSessionInit() {
    if (!earsLinkBegin()) return false;
    conversationSetInputPort({start,stop});
    controlDrops=earsLinkStats().eventDropped;
    return true;
}
void earsSessionSetExtraEventSink(EarsExtraEventSink sink) { extraSink=sink; }
EarsSessionStats earsSessionStats() { return stats; }
EarsSessionNotice earsSessionPoll() {
    notice=EarsSessionNotice::None;
#if PIUU_UART_DIAGNOSTICS
    static bool havePoll=false;
    static uint32_t previousPoll=0;
    const uint32_t now=millis();
    if(havePoll && uint32_t(now-previousPoll)>stats.pollGapMaxMs)
        stats.pollGapMaxMs=uint32_t(now-previousPoll);
    previousPoll=now;havePoll=true;
#endif
    const uint32_t dropped=earsLinkStats().eventDropped;
    if (dropped!=controlDrops) {
        controlDrops=dropped;conversationAbort(ConversationCloseReason::EarsControlLost);
        notice=EarsSessionNotice::ControlLost;appLog().printf("[EARS] abort: event_queue_overflow total=%lu\n",(unsigned long)dropped);
        // Mark the boundary even if the UART worker currently owns the queue
        // mutex. Old events cannot restart the aborted turn on a later poll.
        discardThrough=earsLinkReceivedOrder();discardQueued=true;
        return notice;
    }
    if (conversationState()!=ConversationState::Idle && !earsLinkPeerOnline()) {
        appLog().println("[EARS] abort: peer_timeout");conversationAbort(ConversationCloseReason::EarsOffline);notice=EarsSessionNotice::Offline;
    }
    syncKeywords();
    EarsMessage message{};
    for (int i=0;i<8 && earsLinkTake(message);++i) {
        const uint32_t order=message.kind==EarsProtocol::Kind::Pcm ? message.audio.order : message.event.order;
        if (discardQueued && int32_t(order-discardThrough)<=0) continue;
        if (message.kind==EarsProtocol::Kind::Pcm) handleAudio(message.audio);
        else handleEvent(message.event);
    }
    syncKeywords();
    return notice;
}
