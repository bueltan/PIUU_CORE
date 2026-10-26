#pragma once
#include <stdint.h>
#include <math.h>

namespace Eyes {
constexpr int Width = 320, Height = 240;
struct Config {
    int16_t offsetX = 0, offsetY = 0;
    uint16_t sizePercent = 100; // Eye geometry only; spacing and offsets remain in pixels.
    uint16_t separation = 104; // Center-to-center distance, in pixels.
    uint16_t color = 0x145f; // Approximate RGB565 #168BFF.
    uint16_t backgroundColor = 0x0000;
    uint8_t roundness = 0; // 0: original prototype geometry; 1..100: rounded contour style.
    uint16_t transitionMs = 260;
};
struct Eye { float width, height, lid, slope, arch; };
struct Pose { Eye left, right; float gazeX, gazeY; };
enum class Expression : uint8_t {
    Rest, Connecting, Listening, Thinking, Speaking, Error,
    Happy, Curious, Surprised, Sad, Sleepy, Blink, Count
};
const char* name(Expression expression);
Pose preset(Expression expression);
Pose blend(const Pose& from, const Pose& to, float amount);
class Animator {
public:
    Animator();
    Pose sample(uint32_t now) const;
    void setPose(const Pose& pose, uint32_t now, uint16_t duration);
    void set(Expression expression, uint32_t now, uint16_t duration);
private:
    Pose from_, to_;
    uint32_t started_ = 0;
    uint16_t duration_ = 0;
};
}
