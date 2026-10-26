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
ConversationState conversationState(){return ConversationState::Idle;}
void (*beforeReceive)()=nullptr;
static void udp(const char* text,uint32_t seq) {
    udpSink({SessionProtocol::Kind::Eyes,0,id,seq,reinterpret_cast<const uint8_t*>(text),uint16_t(strlen(text))});
}
static void uart(const char* text){uartSink(reinterpret_cast<const uint8_t*>(text),strlen(text));}
static void settle(){for(unsigned i=0;i<120;++i){++now;eyesPoll();}}
static bool hasColor(uint16_t color){for(auto p:tft.screen)if(p==color)return true;return false;}
int main(){
    eyesInit();eyesShow();assert(tft.rotation==1 && tft.swap);
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
    eyesHide();assert(tft.rotation==0 && !tft.swap);unsigned calls=tft.calls;
    uart("color green");settle();assert(tft.calls==calls);
    for(auto& pixel:tft.screen)pixel=0x7777;
    eyesShow();settle();assert(hasColor(0x07e0) && !hasColor(0x7777));
    puts("PASS: runtime concurrent arrival, replay, saturation, stale responses and UI display ownership");
}
