#include "eyes/runtime.h"
#include "hardware/display_hal.h"
#include "network/server_link.h"
#include "input/ears_link.h"
#include "app/conversation.h"
#include <cassert>
#include <cstring>
#include <cstdio>
Logger Serial;
static uint32_t now=100;
uint32_t millis(){return now;}
uint32_t micros(){return now*1000;}
static TFT_eSPI tft;
TFT_eSPI& display(){return tft;}
static ServerEyesSink udpSink;
static EarsEyesSink uartSink;
void serverLinkSetEyesSink(ServerEyesSink sink){udpSink=sink;}
void earsLinkSetEyesSink(EarsEyesSink sink){uartSink=sink;}
static SessionProtocol::Identity id{1,2,3,4};
SessionProtocol::Identity serverLinkIdentity(){return id;}
static ConversationState state=ConversationState::Idle;
static uint32_t played=0;
static bool clockReady=false;
static unsigned planAcks=0;
bool audioDownlinkVisualPosition(uint32_t& p){p=played;return clockReady;}
static bool plansEnabled=true;
bool serverLinkEyesPlansEnabled(){return plansEnabled;}
bool serverLinkAcknowledgeEyesPlan(SessionProtocol::Identity identity,uint32_t seq){
    assert(SessionProtocol::sameTurn(identity,id) && identity.response==id.response && seq==1);++planAcks;return true;
}
ConversationState conversationState(){return state;}
void (*beforeReceive)()=nullptr;
static void udp(const char* text,uint32_t seq) {
    udpSink({SessionProtocol::Kind::Eyes,0,id,seq,reinterpret_cast<const uint8_t*>(text),uint16_t(strlen(text))});
}
static void uart(const char* text){uartSink(reinterpret_cast<const uint8_t*>(text),strlen(text));}
static const uint8_t planBody[]={1,2,0,0,1,7,9,0x60,0,0,0,0,2,2,7,8,0,0,9,0x60};
static void scheduled(){udpSink({SessionProtocol::Kind::EyesPlan,0,id,1,planBody,sizeof(planBody)});}
static void settle(){for(unsigned i=0;i<120;++i){++now;eyesPoll();}}
static bool hasColor(uint16_t color){for(auto p:tft.screen)if(p==color)return true;return false;}
int main(){
    eyesInit();eyesShow();assert(tft.rotation==3 && tft.swap);
    // Arrival during queue draining must not wrap age to UINT32_MAX.
    beforeReceive=[]{++now;udp("color red",1);};
    eyesPoll();settle();assert(hasColor(0xf800));
    udp("color green",1);settle();assert(hasColor(0xf800) && !hasColor(0x07e0));
    udp("emotion sad 3000",2);uart("size 150");settle();
    // Saturate visual queue; discard the fifth, keep the first four in order.
    for(unsigned n=3;n<7;++n)udp("color red",n);
    udp("color green",7);settle();assert(!hasColor(0x07e0));
    udp("color green",7);now+=1001;settle();assert(!hasColor(0x07e0));
    udp("color green",8);++id.response;settle();assert(!hasColor(0x07e0));
    // Hidden controller accepts settings but cannot draw over the legacy UI.
    eyesHide();assert(tft.rotation==3 && !tft.swap);unsigned calls=tft.calls;
    uart("color green");settle();assert(tft.calls==calls);
    for(auto& pixel:tft.screen)pixel=0x7777;
    eyesShow();settle();assert(hasColor(0x07e0) && !hasColor(0x7777));
    // Plan ACK precedes playback; wall time cannot advance its geometry.
    ++id.response;state=ConversationState::WaitingResponse;
    scheduled();uart("duration 0");settle();assert(planAcks==1);
    scheduled();settle();assert(planAcks==2);
    state=ConversationState::Playing;clockReady=true;played=0;settle();
    calls=tft.calls;now+=10000;settle();assert(tft.calls==calls); // Media clock paused.
    played=100;settle();assert(tft.calls>calls);
    calls=tft.calls;scheduled();settle();assert(tft.calls==calls && planAcks==3);
    uart("background blue");settle();assert(tft.screen[0]==0x145f); // Config still accepted.
    played=2500;settle();played=4200;settle();
    scheduled();settle();assert(planAcks==4); // Expired plan ACKs but does not restart.
    ++id.response;state=ConversationState::WaitingResponse;clockReady=false;
    scheduled();settle();assert(planAcks==5);
    state=ConversationState::Listening;settle(); // Local interruption before server ID reset.
    scheduled();settle();assert(planAcks==5);
    ++id.response;state=ConversationState::WaitingResponse;
    scheduled();settle();udp("gesture blink",100);settle();
    scheduled();settle();assert(planAcks==7); // Duplicate cannot restore the cancelled plan.
    ++id.response;plansEnabled=false;scheduled();settle();assert(planAcks==7);
    plansEnabled=true;
    uint8_t bad[sizeof(planBody)];memcpy(bad,planBody,sizeof(bad));bad[5]=255;
    udpSink({SessionProtocol::Kind::EyesPlan,0,id,1,bad,sizeof(bad)});settle();assert(planAcks==7);
    puts("PASS: runtime concurrent arrival, replay, saturation, stale responses and UI display ownership");
}
