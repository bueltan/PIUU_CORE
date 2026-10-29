#pragma once
#include <stddef.h>
#include "app/conversation_state.h"
// Controller-only except the event sink, which is non-blocking and worker-safe.
bool conversationInit();
ConversationState conversationState();
bool conversationStart();
// Idle-only proactive wake; microphone stays muted until reminder playback ends.
bool conversationStartNotification();
void conversationClose();
void conversationAbort();
void conversationPoll();
bool conversationPostPlaybackEvent(PlaybackEvent event);
enum class ConversationAction { Started, Stopped, Busy, NoWifi, ServicesUnavailable, NoServer, InputUnavailable };
ConversationAction conversationToggleInput();

// Controller receives observed input level without owning UART or a detector task.
void conversationObserveAudio(const int16_t *samples, size_t count);

void conversationLogDiagnostics();

// Main-loop only: discard microphone PCM and wake events during reply/echo tail.
bool conversationInputMuted();

// KEYWORD stays active during Listening/Connecting/WaitingResponse. Only actual
// playback and its short echo guard suppress it; microphone gating is separate.
bool conversationKeywordsMuted();
