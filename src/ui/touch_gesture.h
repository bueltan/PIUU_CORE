#pragma once
#include "input/touch_input.h"
#include "ui/touch_phase.h"
struct UiTouch { uint16_t x,y; TouchPhase phase; };
// Controller-only gesture state. Reject queued presses from a previous view.
class TouchGesture {
    bool held=false;
    uint16_t x=0,y=0;
    uint32_t revision=0;
public:
    bool consume(TouchEvent event,uint32_t viewRevision,uint32_t enteredMs,UiTouch &out) {
        if(event.edge==TouchEdge::Down) {
            held=false;
            if(int32_t(event.capturedMs-enteredMs)<0) return false;
            held=true;x=event.x;y=event.y;revision=viewRevision;
            out={x,y,TouchPhase::Press};return true;
        }
        if(!held) return false;
        held=false;
        if(revision!=viewRevision) return false;
        const int dx=int(event.x)-x,dy=int(event.y)-y;
        out={x,y,dx*dx+dy*dy<=900 ? TouchPhase::Release : TouchPhase::Cancel};
        return true;
    }
};
