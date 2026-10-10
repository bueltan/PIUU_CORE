#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr uint32_t SAMPLE_RATE_HZ = 16000;
constexpr uint8_t AUDIO_CHANNELS = 1;
constexpr uint16_t PCM_FRAME_SAMPLES = 320;
constexpr size_t PCM_FRAME_BYTES = PCM_FRAME_SAMPLES * sizeof(int16_t);
constexpr uint16_t OPUS_FRAME_SAMPLES = 320;
constexpr uint16_t MAX_DECODE_SAMPLES = OPUS_FRAME_SAMPLES;
static_assert(PCM_FRAME_SAMPLES * 1000 / SAMPLE_RATE_HZ == 20, "20 ms PCM frames required");
