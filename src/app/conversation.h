#pragma once
#include <stddef.h>
#include "app/conversation_state.h"
// Controller-only except the event sink, which is non-blocking and worker-safe.
bool conversationInit();
ConversationState conversationState();
bool conversationStart();
// Resume only microphone forwarding, preserving transport turn/context and PCM sequence.
bool conversationResumeInput();
bool conversationInputStandby();
// Idle-only proactive wake; microphone stays muted until reminder playback ends.
bool conversationStartNotification();
// Reserve an existing standby session for a reminder; no new START or input reset.
bool conversationAcceptStandbyNotification();
enum class ConversationCloseReason : uint8_t {
    UserStop, ProviderIdle, SpeechSilence, SpeechWatchdog, TransportFailure,
    WifiLost, PlaybackStalled, EarsOffline, EarsRestarted, EarsControlLost,
    ServerClosed, NotificationTimeout, ConfigurationChanged, StandbyTimeout
};
struct ConversationCloseInfo {
    uint32_t sequence=0, atMs=0, epoch=0, providerQuietMs=0, localQuietMs=0;
    ConversationState state=ConversationState::Idle;
    ConversationCloseReason reason=ConversationCloseReason::UserStop;
    bool speaking=false, muted=false;
};
// Controller-only snapshot, retained in RAM even when diagnostic output is disabled.
ConversationCloseInfo conversationLastClose();
const char *conversationCloseReasonName(ConversationCloseReason reason);
const char *conversationCloseReasonLabel(ConversationCloseReason reason);
void conversationClose(ConversationCloseReason reason=ConversationCloseReason::UserStop);
void conversationAbort(ConversationCloseReason reason=ConversationCloseReason::UserStop);
void conversationPoll();
bool conversationPostPlaybackEvent(PlaybackEvent event);
enum class ConversationAction { Started, Resumed, Stopped, Busy, NoWifi, ServicesUnavailable, NoServer, InputUnavailable };
ConversationAction conversationToggleInput();

// Controller receives observed input level without owning UART or a detector task.
void conversationObserveAudio(const int16_t *samples, size_t count);

void conversationLogDiagnostics();

// Main-loop only: discard microphone PCM and wake events during reply/echo tail.
bool conversationInputMuted();

// KEYWORD stays active during Listening/Connecting/WaitingResponse. Only actual
// playback and its short echo guard suppress it; microphone gating is separate.
bool conversationKeywordsMuted();
