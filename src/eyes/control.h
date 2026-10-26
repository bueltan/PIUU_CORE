#pragma once
#include "engine/gestures.h"
#include "engine/emotion_timer.h"
#include <stddef.h>

namespace EyesControl {
constexpr size_t MaxCommand=31;
enum class Source : uint8_t { Uart, Udp };
enum class Op : uint8_t { Emotion, Gesture, Expression, X, Y, Gap, Size, Color, Background, Roundness, Duration };
struct Command { Op op; int32_t value; uint32_t holdMs; };
bool parse(const uint8_t* data,size_t size,Command& command);
inline bool visual(const Command& c) { return c.op<=Op::Expression; }
class Controller {
public:
    bool apply(const Command&,Source,uint32_t now);
    Eyes::Pose sample(uint32_t now,bool speaking);
    void releaseUdp(uint32_t now);
    bool udpPriority(uint32_t now) const { return leaseMs_ && uint32_t(now-leaseStart_)<leaseMs_; }
    const Eyes::Config& config() const { return config_; }
    Eyes::Emotion emotion() const { return emotion_; }
private:
    Eyes::Config config_;
    Eyes::Animator animator_;
    Eyes::Gestures gestures_;
    Eyes::EmotionTimer timer_;
    Eyes::Emotion emotion_=Eyes::Emotion::Neutral;
    bool emotionMode_=true,udpVisual_=false,udpGesture_=false;
    uint32_t leaseStart_=0,leaseMs_=0;
};
}
