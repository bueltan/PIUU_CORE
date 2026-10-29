#pragma once
#include "config/volume_config.h"
#include "config/audio_format.h"
#include <stddef.h>
#include <stdint.h>

// Controller publishes the target; only the playback task owns the gain ramp.
bool audioVolumeSetPercent(unsigned percent);
unsigned audioVolumePercent();
int32_t audioVolumeGainQ16();
int32_t audioVolumeGainForPercent(unsigned percent);
class AudioVolumeRamp {
public:
    void setTarget(int32_t gain) {
        if (gain == target_) return;
        start_ = current_;
        target_ = gain;
        remaining_ = RampSamples;
    }
    int16_t apply(int16_t sample) {
        if (remaining_) {
            --remaining_;
            current_ = target_ + (start_ - target_) * int32_t(remaining_) / RampSamples;
        }
        // Product fits int32_t even for -32768 at unity. Division preserves
        // signed PCM symmetry and avoids implementation-defined right shifts.
        return int16_t(int32_t(sample) * current_ / VolumeConfig::Unity);
    }
private:
    static constexpr int32_t RampSamples = SAMPLE_RATE_HZ / 200; // 5 ms at 16 kHz.
    int32_t current_ = 0, start_ = 0, target_ = 0, remaining_ = 0;
};
