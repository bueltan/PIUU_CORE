#pragma once
#include <stdint.h>
enum class ConversationEvent : uint8_t { SpeechStarted=1, SpeechStopped, ResponseStarted, Closed, Ready };
// Controller-only. Returns the prior response's conservative playback position.
uint32_t conversationServerEvent(ConversationEvent,uint32_t response,uint32_t utterance);

bool conversationResponseReady();
