#pragma once
#include <Arduino.h>

// All calls belong to the application/controller task. The UART task must
// hand frames to that owner through a bounded queue, never call this concurrently.
using AudioUplinkSender = bool (*)(const uint8_t *, size_t);
void audioUplinkSetSender(AudioUplinkSender sender);
bool audioUplinkBeginConversation();
bool audioUplinkSubmitFrame(const int16_t *pcm, size_t samples);

// Controller-only: disconnect abort, without END/COMMIT.
void audioUplinkClose();

// Preserve timing/sequence metadata of missing UART frames without allocating silence.
void audioUplinkSkipFrames(uint32_t count);
