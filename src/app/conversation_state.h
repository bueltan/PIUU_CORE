#pragma once
#include <stdint.h>
#include "audio/playback_event.h"
enum class ConversationState : uint8_t { Idle, Listening, ClosingInput, WaitingResponse, Playing };
// Owned only by the controller. Pure transitions make event ordering testable.
class ConversationMachine {
public:
    ConversationState state() const { return state_; }
    uint32_t epoch() const { return epoch_; }
    bool begin() {
        if (state_ != ConversationState::Idle) return false;
        nextEpoch(); state_ = ConversationState::Listening; return true;
    }
    bool closeInput() {
        if (state_ != ConversationState::Listening) return false;
        state_ = ConversationState::ClosingInput; return true;
    }
    void inputClosed() {
        if (state_ == ConversationState::ClosingInput) state_ = ConversationState::WaitingResponse;
    }
    void playback(PlaybackEvent event) {
        if (event.epoch != epoch_) return;
        if (state_ != ConversationState::WaitingResponse && state_ != ConversationState::Playing) return;
        state_ = event.kind == PlaybackEventKind::Started ? ConversationState::Playing : ConversationState::Idle;
    }
    void abort() { nextEpoch(); state_ = ConversationState::Idle; }
private:
    ConversationState state_ = ConversationState::Idle;
    uint32_t epoch_ = 0;
    void nextEpoch() { if (++epoch_ == 0) ++epoch_; }
};
