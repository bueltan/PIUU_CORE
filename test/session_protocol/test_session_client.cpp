#include "network/session_client.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace SessionProtocol;
static std::vector<std::vector<uint8_t>> sent;
static bool send(const uint8_t*p,size_t n) {sent.emplace_back(p,p+n);return true;}
static bool startupSendOk=false;
static bool startupSend(const uint8_t *p,size_t n) { send(p,n);return startupSendOk; }
static unsigned helloLogs=0,connectedLogs=0;
static uint32_t helloLocalOk=0,helloElapsed=0,helloAttempts=0;
static void startupTrace(const char *event,Identity,uint32_t a,uint32_t b) {
 if(!strcmp(event,"HELLO_SEND")) { ++helloLogs;helloLocalOk=b; }
 if(!strcmp(event,"HELLO_CONNECTED")) { ++connectedLogs;helloElapsed=a;helloAttempts=b; }
}
static Packet last() {Packet p{};assert(decode(sent.back().data(),sent.back().size(),p));return p;}
static Packet find(Kind k) {for(auto i=sent.rbegin();i!=sent.rend();++i){Packet p{};decode(i->data(),i->size(),p);if(p.kind==k)return p;}assert(false);return {};}
static void ack(SessionClient &c,Kind request,Kind reply,uint32_t now) {auto p=find(request);p.kind=reply;p.flags=1;p.body=nullptr;p.size=0;c.receive(p,now);}
static void handshake(SessionClient&c,uint32_t now) {c.poll(now,true);auto p=find(Kind::Hello);p.kind=Kind::HelloAck;p.id.server=2;p.flags=1;c.receive(p,now);assert(c.ready(now));}
static uint32_t calls=0;
static bool responseReady=false;
static bool eventReady() { return responseReady; }
static uint32_t handleEvent(SessionProtocol::Event,uint32_t,uint32_t) { ++calls;return 123; }
static void put32(uint8_t*p,uint32_t n) {for(int i=3;i>=0;--i){p[i]=n;n>>=8;}}
static void pcm(SessionClient&c,uint32_t seq,uint32_t now) {uint8_t b[658]={};memcpy(b,"PCM!",4);b[4]=1;put32(b+6,seq);put32(b+10,seq*320);assert(c.sendBody(b,sizeof b,now));}
int main(){
 SessionClient c;c.init(1,send);handshake(c,0);assert(c.beginConversation());auto first=c.identity();pcm(c,0,1);c.poll(1,true);assert(find(Kind::Start).id.turn==first.turn);
 auto start=find(Kind::Start);c.poll(151,true);assert(find(Kind::Start).sequence==start.sequence);ack(c,Kind::Start,Kind::StartAck,152);c.poll(152,true);assert(last().kind==Kind::Pcm);
 uint8_t body[6]={0,0,0,0,0,1};Packet nack{Kind::Nack,0,c.identity(),0,body,6};size_t n=sent.size();c.receive(nack,153);c.poll(153,true);assert(sent.size()==n+1&&last().kind==Kind::Pcm);n=sent.size();c.receive(nack,154);c.poll(154,true);assert(sent.size()==n);
 put32(body,99);c.receive(nack,154);assert(last().kind==Kind::Missing&&last().sequence==99);
 c.cancel(123);assert(c.identity().turn==0);assert(c.beginConversation());auto second=c.identity();assert(second.turn>first.turn);c.poll(155,true);assert(last().kind==Kind::Cancel&&last().id.turn==first.turn);ack(c,Kind::Cancel,Kind::CancelAck,156);c.poll(156,true);assert(last().kind==Kind::Start&&last().id.turn==second.turn);
 c.receive({Kind::Error,0,first,0,nullptr,0},157);assert(!c.takeFailure());ack(c,Kind::Start,Kind::StartAck,158);c.cancel(0);c.poll(159,true);ack(c,Kind::Cancel,Kind::CancelAck,160);assert(c.beginConversation());
 c.poll(6201,true);assert(c.takeFailure()&&!c.ready(6201));
 SessionClient d;d.init(7,send);handshake(d,0);assert(d.beginConversation());for(int i=0;i<3;++i)pcm(d,i,1);
 assert(!d.sendBody(reinterpret_cast<const uint8_t*>("COMMIT"),6,1));
 d.poll(1,true);ack(d,Kind::Start,Kind::StartAck,2);d.poll(2,true);d.poll(3,true);
 uint8_t eventBody[5]={3,0,0,0,1};auto response=d.identity();response.response=1;
 d.receive({Kind::Event,0,response,1,eventBody,5},4);assert(d.identity().response==1 && last().kind==Kind::EventAck);
 const auto before=sent.size();d.receive({Kind::Event,0,response,1,eventBody,5},5);assert(sent.size()==before+1);
 pcm(d,3,6);assert(last().kind==Kind::Pcm && last().id.response==0);
 d.playbackFinished(500);assert(d.identity().turn==response.turn && d.identity().response==0);
 d.poll(7,true);assert(last().kind==Kind::Played && last().id.response==1);ack(d,Kind::Played,Kind::PlayedAck,8);
 d.cancel(0);d.poll(9,true);ack(d,Kind::Cancel,Kind::CancelAck,10);assert(d.beginConversation());
 d.poll(11,true);for(int i=1;i<=SessionClient::CONTROL_ATTEMPTS;++i)d.poll(11+150*i,true);assert(d.takeFailure());
 auto retired=d.identity().client;d.reset();assert(d.identity().client!=retired);handshake(d,1500);
 auto previous=d.identity();d.poll(4500,true);auto hello=find(Kind::Hello);hello.kind=Kind::HelloAck;hello.flags=1;hello.id.server=99;
 d.receive(hello,4500);assert(d.takeFailure()&&d.identity().client!=previous.client&&!d.ready(4500));
 SessionClient e;e.init(88,send);handshake(e,0);e.setEventHandler(handleEvent);e.setEventReady(eventReady);
 assert(e.beginConversation());e.poll(1,true);ack(e,Kind::Start,Kind::StartAck,2);
 auto rid=e.identity();rid.response=7;uint8_t responseEvent[5]={3,0,0,0,1};size_t frames=sent.size();
 e.receive({Kind::Event,0,rid,1,responseEvent,5},3);assert(calls==1 && sent.size()==frames);
 e.receive({Kind::Event,0,rid,1,responseEvent,5},4);assert(calls==1 && sent.size()==frames);
 responseReady=true;e.poll(5,true);assert(last().kind==Kind::EventAck && last().body[3]==123);
 e.receive({Kind::Event,0,rid,1,responseEvent,5},6);assert(calls==1 && last().body[3]==123);
 uint8_t speechEvent[5]={1,0,0,0,2};e.receive({Kind::Event,0,rid,3,speechEvent,5},7);assert(calls==1);
 e.receive({Kind::Event,0,rid,2,speechEvent,5},8);assert(calls==2 && e.identity().response==0);
 // A recent matching ACK keeps the link alive despite missing PONGs.
 SessionClient live;live.init(100,send);handshake(live,0);assert(live.beginConversation());live.poll(1,true);
 ack(live,Kind::Start,Kind::StartAck,5900);live.poll(6001,true);assert(!live.takeFailure()&&live.reachable(6001));
 // Media liveness is scoped to the active response; old identities never refresh it.
 live.setEventHandler(handleEvent);auto media=live.identity();media.response=9;
 live.receive({Kind::Event,0,media,1,responseEvent,5},6002);
 live.observeMedia(media,11000);live.poll(12001,true);assert(!live.takeFailure());
 auto staleMedia=media;staleMedia.response=8;live.observeMedia(staleMedia,16000);
 live.poll(17001,true);assert(live.takeFailure());
 // Busy is reachable but cannot admit START. It is not a transport failure.
 SessionClient busy;busy.init(101,send);handshake(busy,0);
 auto busyId=busy.identity();busy.receive({Kind::Pong,0,{busyId.client,busyId.server,0,0},0,nullptr,0},100);
 assert(busy.reachable(100)&&!busy.ready(100)&&!busy.beginConversation()&&!busy.takeFailure());
 busy.receive({Kind::Pong,1,{busyId.client,busyId.server,0,0},0,nullptr,0},200);assert(busy.ready(200));
 assert(busy.beginConversation());busy.poll(201,true);ack(busy,Kind::Start,Kind::StartAck,202);
 busy.receive({Kind::Pong,0,{busyId.client,busyId.server,0,0},0,nullptr,0},300);
 assert(!busy.takeFailure()&&busy.identity().turn&&busy.reachable(300));
 // Unknown, malformed and foreign traffic cannot keep the link alive.
 busy.receive({Kind::Pong,1,{busyId.client,busyId.server+1,0,0},0,nullptr,0},6000);
 busy.receive({Kind::Pong,1,{busyId.client,busyId.server,0,0},0,responseEvent,5},6000);
 busy.poll(6301,true);assert(busy.takeFailure());
 // Unsigned receive ages survive millis rollover.
 SessionClient wrap;wrap.init(102,send);handshake(wrap,UINT32_MAX-100);
 assert(wrap.reachable(50)&&wrap.ready(50));
 SessionClient startup;startup.init(105,startupSend);startup.setObserver(startupTrace);
 startup.poll(100,false);assert(helloLogs==0);
 startup.poll(100,true);assert(helloLogs==1 && helloLocalOk==0);
 startupSendOk=true;startup.poll(600,true);assert(helloLogs==2 && helloLocalOk==1);
 auto reply=find(Kind::Hello);reply.kind=Kind::HelloAck;reply.id.server=2;reply.flags=1;
 startup.receive(reply,650);assert(connectedLogs==1 && helloElapsed==550 && helloAttempts==2);
 startup.poll(3600,true);assert(helloLogs==2); // Healthy HELLO keepalives are silent.
 startup.receive(find(Kind::Hello),3601);assert(connectedLogs==1);
 puts("PASS: v3 ACK retry, continuous PCM, response events/duplicate ACK, playback ACK, NACK once/cache miss, cancellation IDs, normal reuse, timeouts and HELLO diagnostics");
}
