#include "eyes/plan.h"
#include "audio/visual_clock.h"
#include "network/session_client.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>
#include <cstdio>
using namespace EyesControl;
using namespace SessionProtocol;
static const uint8_t bytes[]={1,2,0,0,1,7,9,0x60,0,0,0,0,2,2,7,8,0,0,9,0x60};
static std::vector<uint8_t> sent;
static bool send(const uint8_t* p,size_t n){sent.assign(p,p+n);return true;}
static bool same(Eyes::Pose a,Eyes::Pose b){return !memcmp(&a,&b,sizeof(a));}
int main(int argc,char** argv) {
    Plan decoded;assert(decodePlan(bytes,sizeof(bytes),decoded));
    assert(decoded.count==2 && decoded.steps[0].gesture==Eyes::Gesture::SoftEmphasis && decoded.steps[1].offset==2400);
    for(size_t i=0;i<sizeof(bytes);++i)assert(!decodePlan(bytes,i,decoded));
    for(unsigned index:{0u,1u,2u,3u,4u,5u,6u,11u,19u}) {
        uint8_t bad[sizeof(bytes)];memcpy(bad,bytes,sizeof(bytes));bad[index]=255;
        assert(!decodePlan(bad,sizeof(bad),decoded));
    }
    Eyes::Config config;config.transitionMs=0;
    auto base=Eyes::emotionPose(Eyes::Emotion::Neutral);Eyes::Pose pose;bool done;
    PlanPlayer player;
    assert(player.admit(bytes,sizeof(bytes),false)==PlanAdmission::New && player.priority());
    assert(!player.sample(0,false,base,config,pose,done)); // No playback, no animation.
    assert(player.sample(0,true,base,config,pose,done) && player.step()==0);
    assert(player.sample(100,true,base,config,pose,done));auto moving=pose;
    assert(player.admit(bytes,sizeof(bytes),true)==PlanAdmission::Duplicate);
    assert(player.sample(100,true,base,config,pose,done) && same(moving,pose)); // No restart; paused audio.
    assert(player.sample(0,false,base,config,pose,done) && same(moving,pose)); // Snapshot failure freezes pose.
    uint8_t conflict[sizeof(bytes)];memcpy(conflict,bytes,sizeof(bytes));conflict[4]=5;
    assert(player.admit(conflict,sizeof(conflict),false)==PlanAdmission::Conflict);
    player.sample(2390,true,base,config,pose,done);
    player.sample(2400,true,base,config,pose,done);assert(player.step()==1);
    player.sample(2500,true,base,config,pose,done);
    assert(!same(pose,Eyes::emotionPose(Eyes::Emotion::Curious))); // Planned question overlays the emotion.
    assert(!player.sample(4200,true,base,config,pose,done) && done && !player.priority());
    assert(player.admit(bytes,sizeof(bytes),false)==PlanAdmission::Duplicate && !player.priority());
    player.reset();player.admit(bytes,sizeof(bytes),true); // Late arrival, no historical question pulse.
    assert(player.sample(2500,true,base,config,pose,done) && player.step()==1);
    player.sample(2600,true,base,config,pose,done);assert(same(pose,Eyes::emotionPose(Eyes::Emotion::Curious)));
    player.cancel();assert(!player.priority());
    assert(player.admit(bytes,sizeof(bytes),false)==PlanAdmission::Duplicate && !player.priority());
    player.reset();player.cancel(); // Immediate UDP command overtook the first plan.
    assert(player.admit(bytes,sizeof(bytes),false)==PlanAdmission::New && !player.priority());
    player.reset();player.admit(bytes,sizeof(bytes),true);
    assert(!player.sample(5000,true,base,config,pose,done) && done);
    // Inclusive step duration: transition clamps to remaining audio interval.
    player.reset();player.admit(bytes,sizeof(bytes),true);config.transitionMs=3000;
    player.sample(4190,true,base,config,pose,done);
    assert(!player.sample(4200,true,base,config,pose,done) && done);
    // Visual clock does not jump across missing network sequences or count failed writes.
    VisualClock clock;assert(clock.samples(100)==0);
    clock.submit(320,100);assert(clock.samples(100)==0 && clock.samples(110)==160);
    assert(clock.samples(120)==320 && clock.samples(1000)==320); // Empty rebuffer freezes.
    clock.submit(160,1000);assert(clock.samples(1010)==480);
    clock.submit(0,1100);assert(clock.samples(1200)==480);
    clock={};clock.submit(320,0xfffffff0);assert(clock.samples(4)==320); // millis rollover.
    // Negotiation/ACK identity guard uses the real SessionClient, not a fixture encoder.
    SessionClient client;client.init(41,send);client.poll(0,true);Packet hello{};
    assert(decode(sent.data(),sent.size(),hello) && hello.flags==CAP_EYES_PLAN);
    hello.kind=Kind::HelloAck;hello.id.server=2;hello.flags=7;client.receive(hello,1);
    assert(client.eyesPlansEnabled() && client.beginConversation());
    auto id=client.identity();id.response=1;uint8_t event[]={3,0,0,0,1};
    client.receive({Kind::Event,0,id,1,event,5},2);
    assert(!client.acknowledgeEyesPlan(id,2));auto old=id;++old.response;
    assert(!client.acknowledgeEyesPlan(old,1));assert(client.acknowledgeEyesPlan(id,1));
    Packet ack{};assert(decode(sent.data(),sent.size(),ack) && ack.kind==Kind::EyesPlanAck && !ack.size && !ack.flags);
    client.reset();assert(!client.eyesPlansEnabled() && !client.acknowledgeEyesPlan(id,1));
    if(argc==3) {
        std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> wire((std::istreambuf_iterator<char>(input)),{});
        Packet packet{};assert(decode(wire.data(),wire.size(),packet));
        assert(packet.kind==Kind::EyesPlan && packet.sequence==1 && !packet.flags);
        assert(packet.size==sizeof(bytes) && !memcmp(packet.body,bytes,sizeof(bytes)));
        player.reset();assert(player.admit(packet.body,packet.size,false)==PlanAdmission::New);
        assert(packet.id.client==41 && packet.id.server==2 && packet.id.turn==1 && packet.id.response==1);
        SessionClient wireClient;wireClient.init(41,send);wireClient.poll(0,true);decode(sent.data(),sent.size(),hello);
        hello.kind=Kind::HelloAck;hello.id.server=2;hello.flags=7;wireClient.receive(hello,1);assert(wireClient.beginConversation());
        wireClient.receive({Kind::Event,0,packet.id,1,event,5},2);
        assert(wireClient.acknowledgeEyesPlan(packet.id,packet.sequence));
        std::ofstream(argv[2],std::ios::binary).write(reinterpret_cast<const char*>(sent.data()),sent.size());
    }
    puts("PASS: scheduled eye plan validation, playback pauses, late arrival, dedup, cancellation, wire negotiation and visual clock");
}
