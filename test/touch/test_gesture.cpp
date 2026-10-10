#include "ui/touch_gesture.h"
#include "app/conversation_state.h"
#include <cassert>
#include <cstdio>
int main() {
    TouchGesture gesture;UiTouch out{};
    assert(!gesture.consume({TouchEdge::Up,10,10,101},1,100,out));
    assert(!gesture.consume({TouchEdge::Down,10,10,99},1,100,out));
    assert(gesture.consume({TouchEdge::Down,10,10,101},1,100,out) && out.phase==TouchPhase::Press);
    assert(gesture.consume({TouchEdge::Up,12,12,102},1,100,out) && out.phase==TouchPhase::Release && out.x==10);
    assert(!gesture.consume({TouchEdge::Up,12,12,103},1,100,out));
    gesture.consume({TouchEdge::Down,10,10,110},1,100,out);
    assert(gesture.consume({TouchEdge::Up,100,100,120},1,100,out) && out.phase==TouchPhase::Cancel);
    gesture.consume({TouchEdge::Down,10,10,130},1,100,out);
    assert(!gesture.consume({TouchEdge::Up,10,10,140},2,135,out));
    assert(gesture.consume({TouchEdge::Down,10,10,10},3,UINT32_MAX-10,out));
    assert(gesture.consume({TouchEdge::Up,10,10,11},3,UINT32_MAX-10,out));
    ConversationMachine machine;
    assert(!machine.closeInput());assert(machine.begin());const auto first=machine.epoch();
    machine.playback({first,PlaybackEventKind::Started});assert(machine.state()==ConversationState::Listening);
    assert(machine.closeInput());assert(!machine.closeInput());machine.inputClosed();
    machine.playback({first-1,PlaybackEventKind::Finished});assert(machine.state()==ConversationState::WaitingResponse);
    machine.playback({first,PlaybackEventKind::Finished});assert(machine.state()==ConversationState::Idle); // empty response
    assert(machine.begin());machine.closeInput();machine.inputClosed();machine.abort();
    machine.playback({first+1,PlaybackEventKind::Started});assert(machine.state()==ConversationState::Idle);
    puts("PASS: press/release/cancel, stale gestures, clock rollover, empty response and stale playback epochs");
}
