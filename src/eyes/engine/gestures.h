#pragma once
#include "eyes.h"
namespace Eyes {
enum class Emotion:uint8_t {Neutral,Happy,Curious,Calm,Concerned,Sad,Excited,Count};
enum class Gesture:uint8_t {None,Emphasis,Question,Acknowledge,Glance,Blink,DoubleBlink,SoftEmphasis,Sway,Count};
const char* name(Emotion e);
const char* name(Gesture g);
bool parseEmotion(const char*,Emotion&);
bool parseGesture(const char*,Gesture&);
Pose emotionPose(Emotion e);
class Gestures {
public:
    void setSpeaking(bool enabled,uint32_t now);
    void setIdle(bool enabled,uint32_t now);
    bool speaking() const{return speaking_;}
    bool trigger(Gesture g,uint32_t now);
    void cancel(uint32_t now) { active_=Gesture::None; duration_=0; next_=now+2500; }
    Pose apply(Pose base,Emotion emotion,uint32_t now);
private:
    bool speaking_=false,idle_=false;
    int8_t glanceDirection_=1;
    Gesture active_=Gesture::None;
    uint32_t start_=0,next_=0;
    uint16_t duration_=0;
    uint8_t sequence_=0;
};
}
