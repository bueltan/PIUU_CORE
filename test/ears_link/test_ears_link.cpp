#include "input/ears_link.h"
#include "config/ears_config.h"
#include "Arduino.h"
#include "freertos/task.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <cassert>
#include <cstdio>
HardwareSerial Serial;
HardwareSerial Serial2;
static std::atomic<uint32_t> now{1000};
uint32_t millis() { return now.load(); }
void vTaskDelay(TickType_t) { hostCheckStop();std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
template<class Predicate> void waitFor(Predicate predicate) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()) { assert(std::chrono::steady_clock::now()<end);std::this_thread::yield(); }
}
static std::vector<uint8_t> event(uint8_t code,std::vector<uint8_t> payload={}) {
    std::vector<uint8_t> p={'E','V','T','!',1,code,uint8_t(payload.size()),0};p.insert(p.end(),payload.begin(),payload.end());return p;
}
static std::vector<uint8_t> pcm(uint32_t seq) {
    std::vector<uint8_t> p={'P','C','M','!',uint8_t(seq),uint8_t(seq>>8),uint8_t(seq>>16),uint8_t(seq>>24),128,2};
    for(int i=0;i<320;++i) { p.push_back(0xfe);p.push_back(0xff); }return p;
}
int main() {
    assert(!earsLinkRequestStreaming(true) && !earsLinkPeerOnline());
    assert(earsLinkBegin() && earsLinkBegin());
    assert(Serial2.baud==921600 && Serial2.rxPin==16 && Serial2.txPin==17 && Serial2.rxBuffer==4096);
    assert(Serial.baud==0 && Serial.output().empty()); // USB console untouched.
    waitFor([]{return Serial2.output().size()>=8;});
    assert(Serial2.output()==std::vector<uint8_t>({'C','M','D','!',1,2,0,0}));
    Serial2.inject(event(0x80,{2}));waitFor([]{return earsLinkStats().parseRejected==1;});assert(!earsLinkPeerOnline());
    Serial2.inject(event(0x80,{0}));waitFor([]{return earsLinkPeerOnline();});
    EarsMessage m{};assert(!earsLinkTake(m)); // ACK is consumed by transport.
    assert(earsLinkRequestStreaming(true));waitFor([]{return Serial2.output().size()>=16;});
    auto bytes=Serial2.output();assert(bytes[13]==1);
    Serial2.inject(event(1,{17}));Serial2.inject(pcm(7));Serial2.inject(event(42,{5}));
    waitFor([]{return earsLinkReceivedOrder()>=4;}); // ACK order=1, wake=2, PCM=3, extra=4
    waitFor([&]{return earsLinkTake(m);});assert(m.kind==EarsProtocol::Kind::Event && m.event.code==1);
    waitFor([&]{return earsLinkTake(m);});assert(m.kind==EarsProtocol::Kind::Pcm && m.audio.sequence==7 && m.audio.samples[0]==-2);
    waitFor([&]{return earsLinkTake(m);});assert(m.kind==EarsProtocol::Kind::Event && m.event.code==42);
    // Saturating audio cannot consume control queue capacity.
    for(int i=0;i<8;++i) Serial2.inject(pcm(20+i));
    Serial2.inject(event(3));waitFor([]{return earsLinkStats().audioReceived==9 && earsLinkStats().audioDropped==2;});
    waitFor([]{return earsLinkReceivedOrder()>=13;});
    for(int i=0;i<6;++i) { waitFor([&]{return earsLinkTake(m);});assert(m.kind==EarsProtocol::Kind::Pcm && m.audio.sequence==uint32_t(20+i)); }
    waitFor([&]{return earsLinkTake(m);});assert(m.kind==EarsProtocol::Kind::Event && m.event.code==3);
    for(int i=0;i<10;++i) Serial2.inject(event(42));
    waitFor([]{return earsLinkStats().eventDropped==2;});
    for(int i=0;i<8;++i) { waitFor([&]{return earsLinkTake(m);});assert(m.event.code==42); }
    const auto sent=earsLinkStats().commandsSent;now+=500;
    waitFor([&]{return earsLinkStats().commandsSent>sent;}); // Reaffirm desired state, recover lost CMD/peer reboot.
    now+=2501;assert(!earsLinkPeerOnline());
    Serial2.inject(event(0x80,{1}));waitFor([]{return earsLinkPeerOnline() && earsLinkStats().remoteStreaming;});
    const auto previous=earsLinkStats().commandsSent;assert(earsLinkRequestStreaming(false));
    waitFor([&]{return earsLinkStats().commandsSent>previous;});bytes=Serial2.output();assert(bytes[bytes.size()-3]==2);
    hostStopWorkers();
    puts("PASS: UART2 pin/baud setup, command refresh, ACK validation, ordered queues, saturation isolation and peer timeout");
}
