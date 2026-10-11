#pragma once
#include <stdint.h>
#include "audio/playback_event.h"
// Input remains active throughout every non-Idle state.
enum class ConversationState : uint8_t { Idle, Connecting, Listening, WaitingResponse, Playing };
class ConversationMachine {
public:
    ConversationState state() const { return state_; }
    uint32_t epoch() const { return epoch_; }
    bool begin() {
        if(state_!=ConversationState::Idle) return false;
        nextEpoch();state_=ConversationState::Connecting;return true;
    }
    void ready() { if(state_==ConversationState::Connecting) state_=ConversationState::Listening; }
    void speechStarted() { if(state_!=ConversationState::Idle) {nextEpoch();state_=ConversationState::Listening;} }
    void speechStopped() { if(state_!=ConversationState::Idle) state_=ConversationState::WaitingResponse; }
    void responseStarted() { if(state_!=ConversationState::Idle) {nextEpoch();state_=ConversationState::WaitingResponse;} }
    void playback(PlaybackEvent event) {
        if(event.epoch!=epoch_ || (state_!=ConversationState::WaitingResponse && state_!=ConversationState::Playing)) return;
        state_=event.kind==PlaybackEventKind::Started ? ConversationState::Playing : ConversationState::Listening;
    }
    void abort() { nextEpoch();state_=ConversationState::Idle; }
private:
    ConversationState state_=ConversationState::Idle;
    uint32_t epoch_=0;
    void nextEpoch() { if(++epoch_==0) ++epoch_; }
};
