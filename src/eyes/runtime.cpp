#include "hardware/display_geometry.h"
#include "runtime.h"
#include "control.h"
#include "admission.h"
#include "plan.h"
#include "audio/audio_downlink.h"
#include "engine/renderer.h"
#include "hardware/display_hal.h"
#include "hardware/app_log.h"
#include "network/server_link.h"
#include "input/ears_link.h"
#include "app/conversation.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>
namespace {
using namespace EyesControl;
struct Pending { SessionProtocol::Identity id;uint32_t sequence,received;uint8_t size;bool plan;uint8_t text[MaxCommand]; };
constexpr unsigned Depth=4;
StaticQueue_t udpQueueState,uartQueueState;
uint8_t udpStorage[Depth*sizeof(Pending)],uartStorage[Depth*sizeof(Pending)];
QueueHandle_t udpQueue=nullptr,uartQueue=nullptr;
Controller controller;
Admission admission;
PlanPlayer plan;
uint32_t plansAccepted=0,planDuplicates=0,planRejected=0,planAcks=0,planAckFailed=0;
bool visible=false,havePose=false;
Eyes::Pose lastPose{};
Eyes::Config lastConfig;
uint32_t nextFrame=0,frames=0,maxStep=0,maxFrame=0,frameStart=0,renderUs=0,statsSince=0;
std::atomic<uint32_t> drops{0};
uint32_t accepted=0,invalid=0,stale=0,suppressed=0;
Eyes::Renderer& renderer() { static Eyes::Renderer value(display());return value; }
bool sameConfig(const Eyes::Config& a,const Eyes::Config& b) {
    return a.offsetX==b.offsetX && a.offsetY==b.offsetY && a.sizePercent==b.sizePercent &&
        a.separation==b.separation && a.color==b.color && a.backgroundColor==b.backgroundColor &&
        a.roundness==b.roundness && a.transitionMs==b.transitionMs;
}
void enqueue(QueueHandle_t queue,const uint8_t* data,size_t size,SessionProtocol::Identity id={},uint32_t sequence=0,bool scheduled=false) {
    if(!queue || !size || size>MaxCommand) { ++drops;return; }
    Pending p{};p.id=id;p.sequence=sequence;p.received=millis();p.size=size;p.plan=scheduled;
    memcpy(p.text,data,size);
    if(xQueueSend(queue,&p,0)!=pdTRUE)++drops;
}
void udp(const SessionProtocol::Packet& p) { enqueue(udpQueue,p.body,p.size,p.id,p.sequence,p.kind==SessionProtocol::Kind::EyesPlan); }
void uart(const uint8_t* data,size_t size) { enqueue(uartQueue,data,size); }
void drain(QueueHandle_t queue,Source source) {
    Pending p{};
    for(unsigned i=0;i<Depth && xQueueReceive(queue,&p,0)==pdTRUE;++i) {
        // RX may enqueue during this drain. Sample after taking the message so
        // an arrival one tick newer than loop entry cannot look ancient.
        const uint32_t now=millis();
        if(p.plan) {
            const auto id=serverLinkIdentity();
            const auto state=conversationState();
            if(!serverLinkEyesPlansEnabled() || p.sequence!=1 || !id.turn || !id.response ||
               !SessionProtocol::sameTurn(p.id,id) || p.id.response!=id.response ||
               (state!=ConversationState::WaitingResponse && state!=ConversationState::Playing) ||
               uint32_t(now-p.received)>1000) { ++planRejected;continue; }
            const auto result=plan.admit(p.text,p.size,state==ConversationState::Playing);
            if(result==PlanAdmission::Invalid || result==PlanAdmission::Conflict) { ++planRejected;continue; }
            if(result==PlanAdmission::New) {
                ++plansAccepted;
                if(plan.priority())controller.releaseUdp(now);
            } else ++planDuplicates;
            if(serverLinkAcknowledgeEyesPlan(p.id,p.sequence))++planAcks;
            else ++planAckFailed; // Lost ACK repaired by duplicate plan; never abort audio.
            continue;
        }
        Command c{};
        if(!parse(p.text,p.size,c)){++invalid;continue;}
        if(uint32_t(now-p.received)>1000 ||
           (source==Source::Udp && !admission.accept(p.id,p.sequence,p.received,now,c))) {++stale;continue;}
        if(visual(c)) {
            if(source==Source::Uart && plan.priority()) { ++suppressed;continue; }
            if(source==Source::Udp) {
                if(plan.priority())controller.neutral(now);
                plan.cancel(); // Includes a tombstone for an in-flight older first plan.
            }
        }
        if(controller.apply(c,source,now))++accepted;
        else ++suppressed;
    }
}
}
void eyesInit() {
    udpQueue=xQueueCreateStatic(Depth,sizeof(Pending),udpStorage,&udpQueueState);
    uartQueue=xQueueCreateStatic(Depth,sizeof(Pending),uartStorage,&uartQueueState);
    serverLinkSetEyesSink(udp);earsLinkSetEyesSink(uart);
    statsSince=millis();
}
void eyesShow() {
    display().setRotation(DisplayGeometry::Rotation);display().setSwapBytes(true);
    renderer().invalidate();visible=true;havePose=false;nextFrame=millis();
}
void eyesHide() {
    visible=false;renderer().invalidate();
    display().setRotation(DisplayGeometry::Rotation);display().setSwapBytes(false);
}
void eyesPoll() {
    if(admission.update(serverLinkIdentity())) {
        if(plan.priority())controller.neutral(millis());
        plan.reset();controller.releaseUdp(millis());
    }
    const auto state=conversationState();
    if(plan.priority() && state!=ConversationState::WaitingResponse && state!=ConversationState::Playing) {
        plan.cancel();controller.neutral(millis()); // Manual interrupt precedes server response invalidation.
    }
    drain(udpQueue,Source::Udp);drain(uartQueue,Source::Uart);
    const uint32_t now=millis();
    auto pose=controller.sample(now,state==ConversationState::Playing);
    uint32_t position=0;bool finished=false;
    const bool playing=state==ConversationState::Playing && audioDownlinkVisualPosition(position);
    Eyes::Pose scheduled{};
    if(plan.sample(position,playing,pose,controller.config(),scheduled,finished))pose=scheduled;
    else if(finished) { controller.neutral(now,&scheduled);pose=controller.sample(now,state==ConversationState::Playing); }
    if(!visible)return;
    auto& r=renderer();
    const uint32_t stepStart=micros();
    if(!r.busy() && int32_t(now-nextFrame)>=0) {
        nextFrame=now+33;
        const auto& config=controller.config();
        if(!havePose || memcmp(&pose,&lastPose,sizeof(pose)) || !sameConfig(config,lastConfig)) {
            frameStart=micros();r.begin(pose,config);lastPose=pose;lastConfig=config;havePose=true;
        }
    }
    if(r.busy()) {
        r.step();
        if(!r.busy()) { ++frames;const uint32_t us=micros()-frameStart;if(us>maxFrame)maxFrame=us; }
    }
    const uint32_t us=micros()-stepStart;renderUs+=us;if(us>maxStep)maxStep=us;
}
void eyesLogDiagnostics() {
#if PIUU_DIAGNOSTICS
    const uint32_t now=millis(),window=now-statsSince;
    appLog().printf("[EYES t=%lu] visible=%d udp_priority=%d accepted=%lu invalid=%lu stale=%lu suppressed=%lu drops=%lu window_ms=%lu frames=%lu max_step_us=%lu frame_max_us=%lu render_us=%lu\n",
        (unsigned long)now,visible,controller.udpPriority(now)||plan.priority(),(unsigned long)accepted,(unsigned long)invalid,
        (unsigned long)stale,(unsigned long)suppressed,(unsigned long)drops.load(),(unsigned long)window,
        (unsigned long)frames,(unsigned long)maxStep,(unsigned long)maxFrame,(unsigned long)renderUs);
    uint32_t position=0;const bool clockReady=audioDownlinkVisualPosition(position);
    appLog().printf("[EYES_PLAN] negotiated=%d active=%d step=%d clock=%d played_est_ms=%lu accepted=%lu duplicates=%lu rejected=%lu ack=%lu ack_fail=%lu\n",
        serverLinkEyesPlansEnabled(),plan.priority(),plan.step(),clockReady,(unsigned long)position,(unsigned long)plansAccepted,
        (unsigned long)planDuplicates,(unsigned long)planRejected,(unsigned long)planAcks,(unsigned long)planAckFailed);
    statsSince=now;frames=maxStep=maxFrame=renderUs=0;
#endif
}
