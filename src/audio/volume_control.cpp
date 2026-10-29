#include "audio/volume_control.h"
#include <atomic>
#include <cmath>
namespace {
std::atomic<unsigned> percent{VolumeConfig::DefaultPercent};
std::atomic<int32_t> targetGain{audioVolumeGainForPercent(VolumeConfig::DefaultPercent)};
}
int32_t audioVolumeGainForPercent(unsigned value) {
    if (!value) return 0;
    if (value >= 100) return VolumeConfig::Unity;
    // 1..100 covers approximately -60..0 dB. Each five points adds 3 dB.
    const float db = (float(value) - 100.0f) * 0.6f;
    return int32_t(std::lround(std::pow(10.0f, db / 20.0f) * VolumeConfig::Unity));
}
bool audioVolumeSetPercent(unsigned value) {
    if (value > 100) return false;
    targetGain.store(audioVolumeGainForPercent(value));
    percent.store(value);
    return true;
}
unsigned audioVolumePercent() { return percent.load(); }
int32_t audioVolumeGainQ16() { return targetGain.load(); }
