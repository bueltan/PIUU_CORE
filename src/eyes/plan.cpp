#include "plan.h"
#include <algorithm>
namespace EyesControl {
bool decodePlan(const uint8_t* bytes,size_t size,Plan& out) {
    if(!bytes || size<12 || size>28 || bytes[0]!=1 || bytes[1]<1 || bytes[1]>3 ||
       bytes[2] || bytes[3] || size!=4+8*size_t(bytes[1]))return false;
    Plan candidate;candidate.count=bytes[1];uint32_t expected=0;
    for(unsigned i=0;i<candidate.count;++i) {
        const auto* p=bytes+4+8*i;
        const uint16_t duration=uint16_t(p[2])<<8|p[3];
        const uint32_t offset=uint32_t(p[4])<<24|uint32_t(p[5])<<16|uint32_t(p[6])<<8|p[7];
        if(p[0]>=uint8_t(Eyes::Emotion::Count) || p[1]>=uint8_t(Eyes::Gesture::Count) ||
           duration<1200 || duration>4000 || offset!=expected)return false;
        candidate.steps[i]={offset,duration,Eyes::Emotion(p[0]),Eyes::Gesture(p[1])};expected+=duration;
    }
    out=candidate;return true;
}
PlanAdmission PlanPlayer::admit(const uint8_t* bytes,size_t size,bool alreadyPlaying) {
    Plan candidate;if(!decodePlan(bytes,size,candidate))return PlanAdmission::Invalid;
    if(seen_) {
        if(candidate.count!=plan_.count)return PlanAdmission::Conflict;
        for(unsigned i=0;i<plan_.count;++i) {
            const auto& a=candidate.steps[i];const auto& b=plan_.steps[i];
            if(a.offset!=b.offset || a.duration!=b.duration || a.emotion!=b.emotion || a.gesture!=b.gesture)
                return PlanAdmission::Conflict;
        }
        return PlanAdmission::Duplicate;
    }
    plan_=candidate;seen_=true;active_=!blocked_;late_=alreadyPlaying;
    return PlanAdmission::New;
}
void PlanPlayer::reset() { *this=PlanPlayer{}; }
void PlanPlayer::cancel() { active_=false;blocked_=true; }
bool PlanPlayer::sample(uint32_t position,bool playing,const Eyes::Pose& base,const Eyes::Config& config,
                        Eyes::Pose& output,bool& finished) {
    finished=false;
    if(!active_)return false;
    if(!playing) { if(!started_)return false;position=previous_; }
    // An intermittent snapshot/read failure must never rewind the animation.
    if(started_ && position<previous_)position=previous_;
    unsigned next=0;
    while(next<plan_.count && position>=plan_.steps[next].offset+plan_.steps[next].duration)++next;
    if(next==plan_.count) {
        output=started_?animator_.sample(position):base;
        active_=false;finished=true;return false;
    }
    if(index_!=int(next)) {
        const auto& s=plan_.steps[next];
        const bool trigger=(!started_ && next==0 && (!late_ || position==0) && position<=100) ||
            (started_ && next==unsigned(index_+1) && previous_<s.offset && position-s.offset<=100);
        if(!started_) { animator_.setPose(base,position,0);gestures_.setSpeaking(true,position); }
        animator_.setPose(Eyes::emotionPose(s.emotion),position,
            uint16_t(std::min(uint32_t(config.transitionMs),s.offset+s.duration-position)));
        gestures_.cancel(position); // Planned gesture replaces automatic motion.
        if(trigger)gestures_.trigger(s.gesture,position);
        index_=next;started_=true;
    }
    previous_=position;
    output=gestures_.apply(animator_.sample(position),plan_.steps[next].emotion,position);
    return true;
}
}
