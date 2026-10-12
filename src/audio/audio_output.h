#pragma once
#include <Arduino.h>
// PCM16 mono at SAMPLE_RATE_HZ. The playback task owns writes.
// The current implementation is I2S DAC; Bluetooth will implement this boundary later.
bool audioOutputInit();
struct AudioOutputWriteResult { int32_t error;size_t bytes; };
AudioOutputWriteResult audioOutputWrite(const int16_t *pcm, size_t samples);

// Startup rollback only, before workers are launched.
void audioOutputDeinit();

// Playback-worker only: stop and clear queued DAC audio on an epoch change.
void audioOutputFlush();
