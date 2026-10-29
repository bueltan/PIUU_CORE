#pragma once
#include <Arduino.h>
#include "audio/playback_event.h"
bool audioDownlinkInit();
bool audioDownlinkStartTasks();
// Install before starting workers. Sink must return immediately; false retries later.
void audioDownlinkSetEventSink(PlaybackEventSink sink);
// Controller posts reset; decoder and jitter reset happen in playback worker only.
void audioDownlinkBeginTurn(uint32_t epoch);
void audioDownlinkExpectResponse();

// Conservative estimate; excludes the DAC DMA capacity, not an acoustic measurement.
uint32_t audioDownlinkPlayedMs();
// Visual-only consumed-media estimate. False until a current-epoch I2S write.
// Includes accepted concealment; freezes after queued media drains in rebuffer.
bool audioDownlinkVisualPosition(uint32_t &positionMs);

void audioDownlinkLogDiagnostics();
// Controller-only bounded sample drain; independent of performance diagnostics.
void audioDownlinkPollDiagnostics();
void audioDownlinkLogStacks();
struct AudioBufferStats { uint32_t buffered, peak, rebufferCount, totalMs, maxMs, activeMs; };
bool audioDownlinkBufferStats(AudioBufferStats &stats);

// Reset has reached the playback worker; safe to acknowledge a new response.
bool audioDownlinkEpochReady();

// Controller polls recovery; no network send or logging in playback worker.
void audioDownlinkPollRecovery();

// Controller failure record before epoch reset; no jitter lock or worker formatting.
void audioDownlinkLogQueueFault();
