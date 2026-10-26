#include "control.h"
#include "engine/command_values.h"
#include <cstring>
namespace EyesControl {
bool parse(const uint8_t* data,size_t size,Command& c) {
    if(!data || !size || size>MaxCommand) return false;
    char text[MaxCommand+1];
    for(size_t i=0;i<size;++i) { if(data[i]<32 || data[i]>126) return false; text[i]=char(data[i]); }
    text[size]=0;
    char* words[4]{}; unsigned count=0; char* save=nullptr;
    for(char* p=strtok_r(text," ",&save);p;p=strtok_r(nullptr," ",&save)) {
        if(count==4) return false;
        words[count++]=p;
    }
    if(count<2 || count>3) return false;
    c={Op::Emotion,0,5000};
    if(!strcmp(words[0],"emotion") || !strcmp(words[0],"expr")) {
        if(count==3) { long ms; if(!Eyes::parseInteger(words[2],ms)||ms<0||ms>600000) return false; c.holdMs=ms; }
        if(!strcmp(words[0],"emotion")) { Eyes::Emotion e; if(!Eyes::parseEmotion(words[1],e))return false;c.value=int(e); }
        else { Eyes::Expression e; if(!Eyes::parseExpression(words[1],e))return false;c.op=Op::Expression;c.value=int(e); }
        return true;
    }
    if(count!=2)return false;
    if(!strcmp(words[0],"gesture")) {
        Eyes::Gesture g=Eyes::Gesture::None;
        if(strcmp(words[1],"none") && !Eyes::parseGesture(words[1],g))return false;
        c.op=Op::Gesture;c.value=int(g);return true;
    }
    if(!strcmp(words[0],"color") || !strcmp(words[0],"background")) {
        uint16_t rgb;if(!Eyes::parseColor(words[1],rgb))return false;
        c.op=!strcmp(words[0],"color")?Op::Color:Op::Background;c.value=rgb;return true;
    }
    struct Field {const char* name;Op op;long low,high;};
    static const Field fields[]={{"x",Op::X,-160,160},{"y",Op::Y,-120,120},{"gap",Op::Gap,0,260},
        {"size",Op::Size,25,200},{"roundness",Op::Roundness,0,100},{"duration",Op::Duration,0,3000}};
    long value;if(!Eyes::parseInteger(words[1],value))return false;
    for(const auto& f:fields)if(!strcmp(words[0],f.name)) {
        if(value<f.low || value>f.high)return false;
        c.op=f.op;c.value=value;return true;
    }
    return false;
}
bool Controller::apply(const Command& c,Source source,uint32_t now) {
    if(visual(c)) {
        if(source==Source::Uart && udpPriority(now))return false;
        if(source==Source::Udp) {
            uint32_t hold=c.op==Op::Gesture?1000:(c.holdMs?c.holdMs+config_.transitionMs:5000);
            uint32_t remaining=udpPriority(now)?leaseMs_-uint32_t(now-leaseStart_):0;
            leaseStart_=now;leaseMs_=hold>remaining?hold:remaining;
        }
        if(c.op!=Op::Gesture)udpVisual_=source==Source::Udp;
        udpGesture_=source==Source::Udp;
        gestures_.cancel(now);
    }
    switch(c.op) {
    case Op::Emotion:
        emotionMode_=true;emotion_=Eyes::Emotion(c.value);
        animator_.setPose(Eyes::emotionPose(emotion_),now,config_.transitionMs);
        timer_.start(now,c.holdMs,config_.transitionMs);break;
    case Op::Expression:
        emotionMode_=false;emotion_=Eyes::Emotion::Neutral;
        animator_.set(Eyes::Expression(c.value),now,config_.transitionMs);
        timer_.start(now,c.holdMs,config_.transitionMs);break;
    case Op::Gesture: gestures_.trigger(Eyes::Gesture(c.value),now);break;
    case Op::X:config_.offsetX=c.value;break;
    case Op::Y:config_.offsetY=c.value;break;
    case Op::Gap:config_.separation=c.value;break;
    case Op::Size:config_.sizePercent=c.value;break;
    case Op::Color:config_.color=c.value;break;
    case Op::Background:config_.backgroundColor=c.value;break;
    case Op::Roundness:config_.roundness=c.value;break;
    case Op::Duration:config_.transitionMs=c.value;break;
    }
    return true;
}
Eyes::Pose Controller::sample(uint32_t now,bool speaking) {
    if(timer_.poll(now)) {
        emotionMode_=true;emotion_=Eyes::Emotion::Neutral;
        animator_.setPose(Eyes::emotionPose(emotion_),now,config_.transitionMs);
    }
    if(gestures_.speaking()!=speaking)gestures_.setSpeaking(speaking,now);
    gestures_.setIdle(emotionMode_&&emotion_==Eyes::Emotion::Neutral,now);
    return gestures_.apply(animator_.sample(now),emotion_,now);
}
void Controller::releaseUdp(uint32_t now) {
    leaseMs_=0;
    if(udpGesture_)gestures_.cancel(now);
    udpGesture_=false;
    if(!udpVisual_)return;
    udpVisual_=false;timer_.cancel();gestures_.cancel(now);
    emotionMode_=true;emotion_=Eyes::Emotion::Neutral;
    animator_.setPose(Eyes::emotionPose(emotion_),now,config_.transitionMs);
}
}
