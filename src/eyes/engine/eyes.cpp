#include "eyes.h"
namespace Eyes {
namespace {
// Constant descriptors stay in flash; no image atlas or dynamic allocation.
const Pose presets[] = {
    {{56,78,0,0,0},{56,78,0,0,0},0,0},
    {{48,68,0,0,0},{48,68,0,0,0},0,0},
    {{54,98,0,0,0},{54,98,0,0,0},0,-3},
    {{56,78,.40f,0,0},{56,78,.40f,0,0},12,-12},
    {{58,86,0,0,0},{58,80,0,0,0},0,0},
    {{56,70,.22f,-.45f,0},{56,70,.22f,.45f,0},0,5},
    {{66,52,0,0,1},{66,52,0,0,1},0,0},
    {{52,100,0,0,0},{52,60,0,0,0},8,-4},
    {{66,112,0,0,0},{66,112,0,0,0},0,-3},
    {{58,64,.42f,-.30f,0},{58,64,.42f,.30f,0},0,12},
    {{62,58,.72f,0,0},{62,58,.72f,0,0},0,8},
    {{62,7,0,0,0},{62,7,0,0,0},0,0}
};
const char* const names[] = {"rest","connecting","listening","thinking","speaking","error","happy","curious","surprised","sad","sleepy","blink"};
static_assert(sizeof(presets)/sizeof(presets[0])==unsigned(Expression::Count),"Expression preset count mismatch");
static_assert(sizeof(names)/sizeof(names[0])==unsigned(Expression::Count),"Expression name count mismatch");
float mix(float a,float b,float t) { return a+(b-a)*t; }
Eye blendEye(const Eye&a,const Eye&b,float t) {
    return {mix(a.width,b.width,t),mix(a.height,b.height,t),mix(a.lid,b.lid,t),mix(a.slope,b.slope,t),mix(a.arch,b.arch,t)};
}
}
const char* name(Expression e) { unsigned i=unsigned(e); return i<unsigned(Expression::Count)?names[i]:"invalid"; }
Pose preset(Expression e) { unsigned i=unsigned(e); return presets[i<unsigned(Expression::Count)?i:0]; }
Pose blend(const Pose&a,const Pose&b,float t) {
    return {blendEye(a.left,b.left,t),blendEye(a.right,b.right,t),mix(a.gazeX,b.gazeX,t),mix(a.gazeY,b.gazeY,t)};
}
Animator::Animator():from_(preset(Expression::Rest)),to_(from_) {}
Pose Animator::sample(uint32_t now) const {
    uint32_t elapsed=now-started_; // Handles millis wraparound.
    if (!duration_ || elapsed>=duration_) return to_;
    float t=float(elapsed)/duration_;
    return blend(from_,to_,t*t*(3-2*t));
}
void Animator::setPose(const Pose& pose,uint32_t now,uint16_t duration) {
    from_=sample(now);to_=pose;started_=now;duration_=duration;
}
void Animator::set(Expression e,uint32_t now,uint16_t duration) {
    setPose(preset(e),now,duration);
}
}
