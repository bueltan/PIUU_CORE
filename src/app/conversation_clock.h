#pragma once
#include <stdint.h>

// Controller-owned logical time. Unresponsive intervals do not consume silence
// deadlines. A transition interval is conservatively excluded (one loop tick).
class ConversationClock {
public:
    void reset(uint32_t now) { wall_=value_=now; online_=true; }
    uint32_t update(uint32_t now,bool online) {
        if(online_ && online) value_+=uint32_t(now-wall_);
        wall_=now;online_=online;return value_;
    }
    bool paused() const { return !online_; }
private:
    uint32_t wall_=0,value_=0;
    bool online_=true;
};
