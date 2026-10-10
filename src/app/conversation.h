#pragma once
#include "app/conversation_state.h"
// Controller-only except the event sink, which is non-blocking and worker-safe.
bool conversationInit();
ConversationState conversationState();
bool conversationStart();
void conversationEndInput();
void conversationAbort();
void conversationPoll();
bool conversationPostPlaybackEvent(PlaybackEvent event);
enum class ConversationAction { Started, Stopped, Busy, NoWifi, ServicesUnavailable, NoServer, InputUnavailable };
ConversationAction conversationToggleInput();
