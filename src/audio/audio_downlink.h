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

void audioDownlinkLogDiagnostics();
void audioDownlinkLogStacks();
