#pragma once
#include "input/touch_input.h"
// Consume all pending edges before polling; never forward the exit touch to UI.
class EyesHold {
public:
    void edge(const TouchEvent& e,uint32_t entered) {
        if(e.edge==TouchEdge::Up) {held_=false;return;}
        held_=int32_t(e.capturedMs-entered)>=0;since_=e.capturedMs;
    }
    bool poll(uint32_t now) {
        if(!held_ || uint32_t(now-since_)<=5000)return false;
        held_=false;return true;
    }
private:
    bool held_=false;
    uint32_t since_=0;
};
