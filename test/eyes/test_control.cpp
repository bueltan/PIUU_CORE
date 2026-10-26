#include "eyes/control.h"
#include "eyes/admission.h"
#include "ui/eyes_hold.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace EyesControl;
static Command command(const char* text) {
    Command c{};assert(parse(reinterpret_cast<const uint8_t*>(text),strlen(text),c));return c;
}
int main() {
    Command c{};
    const char* bad[]={"", "emotion", "emotion happy 1 extra", "emotion angry", "emotion sad -1",
        "emotion sad 600001", "size 24", "size 201", "roundness 101", "x -161", "y 121",
        "gap 261", "duration 3001", "color blu", "background GG0000", "gesture nope",
        "color blue\n", "color blue extra", "size 9999999999999999999999"};
    for(auto text:bad)assert(!parse(reinterpret_cast<const uint8_t*>(text),strlen(text),c));
    uint8_t nul[]={'x',' ',0,'1'};assert(!parse(nul,sizeof(nul),c));
    assert(command("background blue").value==command("color blue").value);
    assert(command("gesture none").value==0);
    Controller ctl;
    assert(ctl.config().roundness==0);
    assert(ctl.apply(command("emotion sad 1200"),Source::Udp,100));
    assert(!ctl.apply(command("emotion happy"),Source::Uart,200));
    assert(!ctl.apply(command("gesture blink"),Source::Uart,200));
    assert(ctl.apply(command("size 150"),Source::Uart,200));
    assert(ctl.config().sizePercent==150);
    assert(ctl.apply(command("gesture question"),Source::Udp,250));
    assert(ctl.udpPriority(1559)); // Gesture did not shorten the emotion lease.
    ctl.sample(1560,false);assert(ctl.emotion()==Eyes::Emotion::Neutral);
    assert(!ctl.udpPriority(1560));
    assert(ctl.apply(command("emotion happy 1000"),Source::Uart,1560));
    assert(ctl.emotion()==Eyes::Emotion::Happy);
    ctl.releaseUdp(1600);assert(ctl.emotion()==Eyes::Emotion::Happy);
    ctl.apply(command("emotion sad 0"),Source::Udp,2000);
    assert(!ctl.udpPriority(7000)); // Persistent pose cannot hold the lease forever.
    ctl.apply(command("gesture blink"),Source::Uart,7000);
    ctl.releaseUdp(7001);assert(ctl.emotion()==Eyes::Emotion::Neutral);
    ctl.apply(command("emotion excited 1000"),Source::Udp,0xffffff00u);
    assert(ctl.udpPriority(500));ctl.sample(1100,false);
    assert(!ctl.udpPriority(1100) && ctl.emotion()==Eyes::Emotion::Neutral);
    auto e=command("emotion happy"),config=command("color red");
    Admission a;SessionProtocol::Identity id{1,2,3,4};
    assert(a.update(id));assert(!a.update(id));
    assert(a.accept(id,0xfffffffe,100,100,e));
    assert(!a.accept(id,0xfffffffe,100,100,e));
    assert(a.accept(id,1,100,100,e)); // Sequence wrap.
    assert(!a.accept(id,0,100,100,e));
    assert(!a.accept(id,2,100,1101,e)); // Expired queue item.
    auto old=id;++id.response;assert(a.update(id));
    assert(!a.accept(old,3,100,100,e));assert(a.accept(id,0,100,100,e));
    id.response=id.turn=0;a.update(id);
    assert(!a.accept(id,1,100,100,e));assert(a.accept(id,1,100,100,config));
    id.server=0;a.update(id);assert(!a.accept(id,2,100,100,config));
    EyesHold hold;
    hold.edge({TouchEdge::Down,0,0,100},100);
    assert(!hold.poll(5100));assert(hold.poll(5101));assert(!hold.poll(6000));
    hold.edge({TouchEdge::Down,0,0,100},100);hold.edge({TouchEdge::Up,0,0,200},100);
    assert(!hold.poll(6000));
    hold.edge({TouchEdge::Down,0,0,99},100);assert(!hold.poll(6000));
    hold.edge({TouchEdge::Down,0,0,0xffffff00u},0xffffff00u);assert(hold.poll(5000));
    puts("PASS: eyes parsing, UDP arbitration, expiry, identity/sequence replay, rollover and touch hold");
}
