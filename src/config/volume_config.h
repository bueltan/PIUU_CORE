#pragma once
#include <stdint.h>
namespace VolumeConfig {
constexpr uint8_t DefaultPercent = 20;
constexpr uint8_t Step = 5;
constexpr int32_t Unity = 65536; // Q16 gain, with no amplification above unity.
}
