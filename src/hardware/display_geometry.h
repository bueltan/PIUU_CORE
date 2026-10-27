#pragma once
#include <stdint.h>

namespace DisplayGeometry {
constexpr uint8_t Rotation = 3;
constexpr uint16_t Width = 320;
constexpr uint16_t Height = 240;
// Keep the existing rotation-0 touch calibration; rotate its output with the panel.
inline void fromPortrait(uint16_t px, uint16_t py, uint16_t &x, uint16_t &y) {
    x = Width - 1 - py;
    y = px;
}
}
