#include "app/device_volume.h"
#include "audio/volume_control.h"
#include "config/settings.h"
#include "network/session_client.h"
#include "hardware/app_log.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
using namespace SessionProtocol;
Logger Serial;
static DeviceSettings config;
static bool saveOk=true;
static unsigned writes=0;
static bool sendOk=true;
static std::string volumeEvent;
static void observe(const char *event,Identity,uint32_t,uint32_t) {
    if(!strncmp(event,"VOLUME_",7)) volumeEvent=event;
}
const DeviceSettings &settings() { return config; }
bool settingsSetVolume(unsigned value) { ++writes;if(!saveOk)return false;config.volumePercent=value;return true; }
static std::vector<std::vector<uint8_t>> sent;
static bool send(const uint8_t *p,size_t n) { sent.emplace_back(p,p+n);return sendOk; }
static Packet last() { Packet p{};assert(decode(sent.back().data(),sent.back().size(),p));return p; }
static void connect(SessionClient &client,bool capable=true) {
    client.setVolumeHandler(deviceVolumeCommand);assert(client.init(700,send));client.poll(0,true);
    auto hello=last();assert(hello.flags&CAP_VOLUME_CONTROL);
    hello.kind=Kind::HelloAck;hello.id.server=2;hello.flags=3|(capable?CAP_VOLUME_CONTROL:0);hello.size=0;
    client.receive(hello,0);assert(client.ready(0));
}
static void command(unsigned operation,int value,bool persist=false) {
    uint8_t request[]={uint8_t(operation),uint8_t(value),uint8_t(persist)},reply[4];
    deviceVolumeCommand(request,reply);assert(reply[0]==0);
}
int main(int argc,char **argv) {
    SessionClient client;connect(client);
    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> wire((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        Packet packet{};assert(decode(wire.data(),wire.size(),packet));
        client.receive(packet,1);assert(last().kind==Kind::VolumeAck);
        fwrite(sent.back().data(),1,sent.back().size(),stdout);return 0;
    }
    client.setObserver(observe);
    command(1,60);command(2,-10);assert(audioVolumePercent()==50);
    command(3,1);command(3,1);assert(audioVolumePercent()==0 && audioVolumeBeforeMute()==50);
    command(3,0);assert(audioVolumePercent()==50);
    // A local screen change becomes the remote mute restore target too.
    audioVolumeSetPercent(35);command(3,1);command(3,0);assert(audioVolumePercent()==35);
    command(2,100);assert(audioVolumePercent()==100);
    command(2,-100);assert(audioVolumePercent()==0);
    command(1,40,true);assert(config.volumePercent==40 && writes==1);
    saveOk=false;
    uint8_t request[]={1,30,1},reply[4];deviceVolumeCommand(request,reply);
    assert(reply[0]==2 && reply[1]==30 && reply[3]==40);
    for(const auto &invalid:std::vector<std::vector<uint8_t>>{{1,101,0},{1,255,0},{2,101,0},{2,155,0},{3,2,0},{0,1,0},{0,0,1},{3,1,1},{4,0,0},{1,20,2}}) {
        deviceVolumeCommand(invalid.data(),reply);assert(reply[0]==1 && audioVolumePercent()==30);
    }
    command(1,50);
    uint8_t delta[]={2,uint8_t(-10),0};
    Packet packet{Kind::Volume,0,client.identity(),1,delta,3};
    client.receive(packet,1);assert(audioVolumePercent()==40 && last().body[1]==40);
    const auto cached=sent.back();
    client.receive(packet,2);assert(sent.back()==cached && audioVolumePercent()==40);
    uint8_t other[]={2,10,0};packet.body=other;
    size_t count=sent.size();client.receive(packet,3);assert(sent.size()==count && audioVolumePercent()==40);
    packet.sequence=2;client.receive(packet,4);assert(audioVolumePercent()==50);
    packet.sequence=1;count=sent.size();client.receive(packet,5);assert(sent.size()==count && audioVolumePercent()==50);
    packet.sequence=3;
    auto good=packet;
    for(int invalid=0;invalid<7;++invalid) {
        packet=good;
        if(invalid==0)++packet.id.client;
        if(invalid==1)++packet.id.server;
        if(invalid==2)++packet.id.turn;
        if(invalid==3)++packet.id.response;
        if(invalid==4)packet.flags=1;
        if(invalid==5)packet.size=2;
        if(invalid==6)packet.sequence=0;
        count=sent.size();client.receive(packet,6);assert(sent.size()==count && audioVolumePercent()==50);
    }
    sendOk=false;client.receive(good,7);
    assert(volumeEvent=="VOLUME_ACK_FAILED" && audioVolumePercent()==60);
    sendOk=true;client.receive(good,8);
    assert(volumeEvent=="VOLUME_ACK_SENT" && audioVolumePercent()==60);
    SessionClient old;connect(old,false);good.id=old.identity();count=sent.size();old.receive(good,9);assert(sent.size()==count);
    client.reset();count=sent.size();client.receive(good,8);assert(sent.size()==count);
#if PIUU_VOLUME_DIAGNOSTICS
    assert(Serial.output.find("[VOLUME] source=remote")!=std::string::npos);
    assert(Serial.output.find("persist=1 status=2 live=30")!=std::string::npos);
#else
    assert(Serial.output.empty());
#endif
    puts("PASS: device volume, clamping, shared mute, persistence failure, duplicate/stale/foreign UDP, capability negotiation");
}
