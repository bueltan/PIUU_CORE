#pragma once
#include <stdint.h>
namespace Eyes {
class EmotionTimer {
public:
    void start(uint32_t now,uint32_t holdMs,uint16_t transitionMs){
        started_=now;total_=holdMs?holdMs+transitionMs:0;
    }
    void cancel(){total_=0;}
    bool poll(uint32_t now){if(total_&&uint32_t(now-started_)>=total_){cancel();return true;}return false;}
    uint32_t remaining(uint32_t now) const {
        if(!total_)return 0;
        uint32_t elapsed=now-started_;return elapsed>=total_?0:total_-elapsed;
    }
private:
    uint32_t started_=0,total_=0;
};
}
