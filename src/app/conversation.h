#pragma once
#include <stddef.h>
#include "app/conversation_state.h"
// Controller-only except the event sink, which is non-blocking and worker-safe.
bool conversationInit();
ConversationState conversationState();
bool conversationStart();
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
