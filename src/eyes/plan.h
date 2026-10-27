#pragma once
#include "engine/gestures.h"
#include <stddef.h>
namespace EyesControl {
struct PlanStep { uint32_t offset;uint16_t duration;Eyes::Emotion emotion;Eyes::Gesture gesture; };
struct Plan { PlanStep steps[3]{};uint8_t count=0; };
bool decodePlan(const uint8_t*,size_t,Plan&);
enum class PlanAdmission { Invalid, Conflict, New, Duplicate };
// One immutable plan per response; reset only on response identity change.
class PlanPlayer {
public:
    PlanAdmission admit(const uint8_t*,size_t,bool alreadyPlaying);
    void reset();
    void cancel(); // Preserve dedup marker; retries cannot restore a cancelled plan.
    bool priority() const { return active_; }
    bool seen() const { return seen_; }
    int step() const { return index_; }
    // Returns true when supplying the pose. finished fires once at final expiry.
    bool sample(uint32_t position,bool playing,const Eyes::Pose& base,const Eyes::Config&,
                Eyes::Pose& output,bool& finished);
private:
    Plan plan_;
    Eyes::Animator animator_;
    Eyes::Gestures gestures_;
    bool seen_=false,active_=false,blocked_=false,late_=false,started_=false;
    int8_t index_=-1;
    uint32_t previous_=0;
};
static_assert(sizeof(Plan)==28,"Plan storage must stay bounded to three compact steps");
static_assert(sizeof(PlanPlayer)<=256,"Plan player must not grow into a frame queue");
}
