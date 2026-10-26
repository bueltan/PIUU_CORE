#include "gestures.h"
#include <string.h>
namespace Eyes {
namespace {
const char* const emotions[]={"neutral","happy","curious","calm","concerned","sad","excited"};
const char* const gestures[]={"none","emphasis","question","acknowledge","glance","blink","doubleblink","soft_emphasis","sway"};
const uint16_t durations[]={0,220,380,280,500,160,420,420,700};
}
const char* name(Emotion e){return unsigned(e)<unsigned(Emotion::Count)?emotions[unsigned(e)]:"invalid";}
const char* name(Gesture g){return unsigned(g)<unsigned(Gesture::Count)?gestures[unsigned(g)]:"invalid";}
bool parseEmotion(const char*s,Emotion&e){for(unsigned i=0;i<unsigned(Emotion::Count);++i)if(!strcmp(s,emotions[i])){e=Emotion(i);return true;}return false;}
bool parseGesture(const char*s,Gesture&g){for(unsigned i=1;i<unsigned(Gesture::Count);++i)if(!strcmp(s,gestures[i])){g=Gesture(i);return true;}return false;}
Pose emotionPose(Emotion e){
    switch(e){
    case Emotion::Happy:return preset(Expression::Happy);
    case Emotion::Curious:return preset(Expression::Curious);
    case Emotion::Concerned:return preset(Expression::Error);
    case Emotion::Sad:return preset(Expression::Sad);
    case Emotion::Calm:{auto p=preset(Expression::Rest);p.left.lid=p.right.lid=.18f;return p;}
    case Emotion::Excited:{auto p=preset(Expression::Surprised);p.left.height=p.right.height=100;return p;}
    default:return preset(Expression::Rest);
    }
}
void Gestures::setSpeaking(bool enabled,uint32_t now){speaking_=enabled;next_=now;}
void Gestures::setIdle(bool enabled,uint32_t now){
    if(enabled!=idle_){idle_=enabled;if(enabled&&!speaking_)next_=now+2500;}
}
bool Gestures::trigger(Gesture g,uint32_t now){
    if(g==Gesture::None||g>=Gesture::Count)return false;
    if(active_!=Gesture::None&&uint32_t(now-start_)<duration_)return false;
    if(g==Gesture::Glance)glanceDirection_=-glanceDirection_;
    active_=g;start_=now;duration_=durations[unsigned(g)];return true;
}
Pose Gestures::apply(Pose p,Emotion emotion,uint32_t now){
    if(active_!=Gesture::None&&uint32_t(now-start_)>=duration_)active_=Gesture::None;
    if(active_==Gesture::None&&speaking_&&int32_t(now-next_)>=0){
        static const Gesture cycle[]={Gesture::Emphasis,Gesture::Acknowledge,Gesture::Glance,Gesture::Question,Gesture::Blink,Gesture::SoftEmphasis,Gesture::Sway,Gesture::DoubleBlink};
        trigger(cycle[sequence_%8],now);sequence_=(sequence_+1)%8;
        next_=now+duration_+((emotion==Emotion::Calm||emotion==Emotion::Sad)?1100:650);
    }
    if(active_==Gesture::None&&!speaking_&&idle_&&int32_t(now-next_)>=0){
        trigger((sequence_%3==1)?Gesture::Glance:Gesture::Blink,now);
        sequence_=(sequence_+1)%3;next_=now+duration_+2500+sequence_*650;
    }
    if(active_==Gesture::None)return p;
    float t=float(uint32_t(now-start_))/duration_;
    // Smooth zero-ended envelope; no persistent offset when the gesture ends.
    float v=t<.5f?t*2:(1-t)*2;float envelope=v*v*(3-2*v);
    if(active_==Gesture::DoubleBlink){
        // Two close/open pulses separated by a pause; each has zero-ended easing.
        float ms=uint32_t(now-start_);float phase=ms<160?ms/160:(ms>=240?(ms-240)/180:0);
        float pulse=phase<.5f?phase*2:(1-phase)*2;
        envelope=pulse*pulse*(3-2*pulse);
    }
    float gain=(emotion==Emotion::Calm||emotion==Emotion::Sad)?.45f:(emotion==Emotion::Excited?1.2f:1.0f);
    float a=envelope*gain;
    switch(active_){
    case Gesture::Emphasis:p.left.height*=1+.08f*a;p.right.height*=1+.08f*a;p.gazeY-=2*a;break;
    case Gesture::Question:p.left.height*=1+.06f*a;p.right.height*=1-.04f*a;p.gazeY-=3*a;break;
    case Gesture::Acknowledge:p.gazeY+=3*a;break;
    case Gesture::Glance:p.gazeX+=glanceDirection_*5*a;break;
    case Gesture::SoftEmphasis:p.left.height*=1+.035f*a;p.right.height*=1+.035f*a;break;
    case Gesture::Sway:p.gazeX+=4*a*sinf(6.2831853f*t);break;
    case Gesture::DoubleBlink:
    case Gesture::Blink:
        p.left.height=p.left.height*(1-envelope)+7*envelope;
        p.right.height=p.right.height*(1-envelope)+7*envelope;
        p.left.lid*=1-envelope;p.right.lid*=1-envelope;
        p.left.slope*=1-envelope;p.right.slope*=1-envelope;
        p.left.arch*=1-envelope;p.right.arch*=1-envelope;break;
    default:break;
    }
    return p;
}
}
