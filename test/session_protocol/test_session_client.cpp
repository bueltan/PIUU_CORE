#include "network/session_client.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
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
static void handshake(SessionClient&c,uint32_t now) {c.poll(now,true);auto p=find(Kind::Hello);p.kind=Kind::HelloAck;p.id.server=2;p.flags=3|(p.flags&CAP_DEVICE_MAC);p.body=nullptr;p.size=0;c.receive(p,now);assert(c.ready(now));}
static SessionClient *noticeClient=nullptr;
static unsigned noticeCalls=0;
static bool onNotice() { ++noticeCalls;return noticeClient->beginConversation(); }
static unsigned standbyCalls=0;
static bool standbyAllowed=false;
static bool onStandbyNotice() { ++standbyCalls;return standbyAllowed; }
static uint32_t calls=0;
static bool responseReady=false;
static bool eventReady() { return responseReady; }
static uint32_t handleEvent(SessionProtocol::Event,uint32_t,uint32_t) { ++calls;return 123; }
static void put32(uint8_t*p,uint32_t n) {for(int i=3;i>=0;--i){p[i]=n;n>>=8;}}
static void pcm(SessionClient&c,uint32_t seq,uint32_t now) {uint8_t b[658]={};memcpy(b,"PCM!",4);b[4]=1;put32(b+6,seq);put32(b+10,seq*320);b[14]=1;b[15]=64;b[16]=2;b[17]=128;assert(c.sendBody(b,sizeof b,now));}
int main(int argc,char **argv){
 if(argc==3 && !strcmp(argv[1],"--standby-notice")) {
  SessionClient device;device.setNotificationHandler(onNotice);device.setStandbyNotificationHandler(onStandbyNotice);
  assert(device.init(700,send));device.poll(0,true);
  auto hello=find(Kind::Hello);hello.kind=Kind::HelloAck;hello.id.server=2;
  hello.flags=3|CAP_NOTIFICATIONS|CAP_STANDBY_NOTIFICATIONS;hello.body=nullptr;hello.size=0;device.receive(hello,0);
  assert(device.beginConversation());device.poll(1,true);ack(device,Kind::Start,Kind::StartAck,2);
  std::ifstream file(argv[2],std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  standbyAllowed=true;Packet notice{};assert(decode(bytes.data(),bytes.size(),notice));device.receive(notice,3);
  assert(last().kind==Kind::NoticeAck && last().flags==1 && device.identity().turn==1);
  fwrite(sent.back().data(),1,sent.back().size(),stdout);return 0;
 }
 if(argc==3 && !strcmp(argv[1],"--notice")) {
  SessionClient device;noticeClient=&device;device.setNotificationHandler(onNotice);
  assert(device.init(700,send));device.poll(0,true);
  auto hello=find(Kind::Hello);hello.kind=Kind::HelloAck;hello.id.server=2;
  hello.flags=3|CAP_NOTIFICATIONS;hello.body=nullptr;hello.size=0;device.receive(hello,0);
  std::ifstream file(argv[2],std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  Packet wake{};assert(decode(bytes.data(),bytes.size(),wake));device.receive(wake,1);device.poll(2,true);
  auto request=find(Kind::Start);assert(request.size==4);
  for(auto it=sent.rbegin();it!=sent.rend();++it) {
   Packet packet{};decode(it->data(),it->size(),packet);
   if(packet.kind==Kind::Start) { fwrite(it->data(),1,it->size(),stdout);return 0; }
  }
  return 1;
 }
 if(argc==2 && !strcmp(argv[1],"--hello")) {
  const uint8_t mac[6]={0x02,0x12,0x34,0x56,0x78,0x9a};
  SessionClient device;assert(device.init(500,send,mac));device.poll(0,true);
  fwrite(sent.back().data(),1,sent.back().size(),stdout);return 0;
 }
 if(argc==2) {
  // Real CORE cache, with 300 ms simulated NACK age; stdout is a v3 PCM datagram.
  SessionClient recovery;recovery.init(40,send);handshake(recovery,0);
  assert(recovery.beginConversation());recovery.poll(1,true);ack(recovery,Kind::Start,Kind::StartAck,2);
  for(uint32_t i=0;i<16;++i) pcm(recovery,i,i*20);
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  Packet request{};assert(decode(bytes.data(),bytes.size(),request));
  recovery.receive(request,300);recovery.poll(300,true);
  assert(recovery.stats().retransmits==1 && last().sequence==0);
  fwrite(sent.back().data(),1,sent.back().size(),stdout);return 0;
 }
 // Negotiated idle notices reuse START and never interrupt an active conversation.
 {
  SessionClient n;noticeClient=&n;n.setNotificationHandler(onNotice);assert(n.init(900,send));
  n.poll(0,true);auto h=find(Kind::Hello);assert(h.flags&CAP_NOTIFICATIONS);
  h.kind=Kind::HelloAck;h.id.server=2;h.flags=3|CAP_NOTIFICATIONS;h.body=nullptr;h.size=0;n.receive(h,0);
  auto session=n.identity();Packet wake{Kind::Notice,0,session,10,nullptr,0};
  auto wrong=wake;wrong.id.server=3;n.receive(wrong,1);assert(!noticeCalls);
  wrong=wake;wrong.flags=1;n.receive(wrong,1);assert(!noticeCalls);
  n.receive(wake,1);assert(noticeCalls==1 && n.identity().turn);
  n.poll(2,true);auto request=find(Kind::Start);
  assert(request.size==4 && request.body[3]==10);
  n.receive(wake,3);assert(noticeCalls==1);
  wake.sequence=11;n.receive(wake,3);assert(noticeCalls==1); // busy
  ack(n,Kind::Start,Kind::StartAck,4);n.cancel(0);n.poll(5,true);ack(n,Kind::Cancel,Kind::CancelAck,6);
  wake.sequence=10;n.receive(wake,7);assert(noticeCalls==1); // delayed duplicate
  wake.sequence=11;n.receive(wake,8);assert(noticeCalls==2);
  n.reset();handshake(n,9);wake.id=n.identity();wake.sequence=12;
  n.receive(wake,10);assert(noticeCalls==2); // old backend did not accept capability
 }
 // Active notices require CORE standby admission; duplicate ACKs never re-admit.
 {
  SessionClient n;n.setNotificationHandler(onNotice);n.setStandbyNotificationHandler(onStandbyNotice);
  assert(n.init(910,send));n.poll(0,true);auto h=find(Kind::Hello);
  assert(h.flags&CAP_STANDBY_NOTIFICATIONS);
  h.kind=Kind::HelloAck;h.id.server=2;h.flags=3|CAP_NOTIFICATIONS|CAP_STANDBY_NOTIFICATIONS;
  h.body=nullptr;h.size=0;n.receive(h,0);assert(n.beginConversation());
  n.poll(1,true);ack(n,Kind::Start,Kind::StartAck,2);const auto id=n.identity();
  Packet notice{Kind::Notice,0,id,20,nullptr,0};
  n.receive(notice,3);assert(standbyCalls==1 && last().kind==Kind::NoticeAck && !last().flags);
  standbyAllowed=true;n.receive(notice,4);assert(standbyCalls==2 && last().flags==1);
  const auto before=sent.size();n.receive(notice,5);assert(standbyCalls==2 && sent.size()==before+1 && last().flags==1);
  auto bad=notice;bad.sequence++;bad.id.turn++;n.receive(bad,6);assert(standbyCalls==2);
  bad=notice;bad.sequence++;bad.flags=1;n.receive(bad,6);assert(standbyCalls==2);
  bad=notice;bad.sequence++;bad.id.response=1;n.receive(bad,6);assert(standbyCalls==2);
  bad=notice;bad.sequence--;n.receive(bad,6);assert(standbyCalls==2);
  assert(n.identity().turn==id.turn && n.identity().response==0);
  uint8_t responseEvent[5]={3,0,0,0,1};auto response=id;response.response=1;
  n.receive({Kind::Event,0,response,1,responseEvent,5},7);
  notice.sequence++;n.receive(notice,8);assert(standbyCalls==2 && !last().flags);
  n.reset();handshake(n,9);assert(n.beginConversation());n.poll(10,true);ack(n,Kind::Start,Kind::StartAck,11);
  notice.id=n.identity();const auto withoutCapability=sent.size();n.receive(notice,12);
  assert(standbyCalls==2 && sent.size()==withoutCapability);
 }
 // Stable device identity survives session resets, including an idle NAT failure.
 {
  const uint8_t mac[6]={0x02,0x12,0x34,0x56,0x78,0x9a};
  SessionClient device;assert(device.init(500,send,mac));handshake(device,0);
  auto hello=find(Kind::Hello);assert((hello.flags&CAP_DEVICE_MAC) && hello.size==6 && !memcmp(hello.body,mac,6));
  const auto old=device.identity();device.poll(6001,true);
  assert(device.takeFailure() && device.identity().client!=old.client);
  handshake(device,6501);hello=find(Kind::Hello);
  assert(!memcmp(hello.body,mac,6));
  device.reset();device.poll(7000,true);auto reply=find(Kind::Hello);
  reply.kind=Kind::HelloAck;reply.flags=3;reply.id.server=2;reply.body=nullptr;reply.size=0;
  device.receive(reply,7001);assert(device.takeFailure() && !device.ready(7001));
  const uint8_t zero[6]={},multicast[6]={1,2,3,4,5,6};
  SessionClient invalid;assert(!invalid.init(1,send,zero) && !invalid.init(1,send,multicast));
 }
 // A requested live frame goes ahead of unsent startup frames, within the budget.
 {
  SessionClient r;r.init(31,send);handshake(r,0);assert(r.beginConversation());
  for(uint32_t i=0;i<8;++i) pcm(r,i,i*20);
  r.poll(160,true);ack(r,Kind::Start,Kind::StartAck,161);pcm(r,8,162);
  uint8_t request[6]={0,0,0,8,0,1};Packet p{Kind::Nack,0,r.identity(),0,request,6};
  const size_t before=sent.size();r.receive(p,180);r.poll(180,true);
  Packet first{};assert(decode(sent[before].data(),sent[before].size(),first));
  assert(first.kind==Kind::Pcm && first.sequence==8);
  assert(sent.size()==before+SessionClient::RECOVERY_BUDGET && r.stats().retransmits==1);
 }
 // Retention, capture-age expiry, invalid masks and failed retry accounting.
 {
  SessionClient r;r.init(30,startupSend);startupSendOk=true;handshake(r,0);
  assert(r.beginConversation());r.poll(1,true);ack(r,Kind::Start,Kind::StartAck,2);
  for(uint32_t i=0;i<16;++i) pcm(r,i,20*i);
  uint8_t request[6]={0,0,0,0,0,1};Packet p{Kind::Nack,0,r.identity(),0,request,6};
  r.receive(p,300);r.poll(300,true);assert(r.stats().retransmits==1 && last().sequence==0);
  r.receive(p,301);r.poll(301,true);assert(r.stats().retransmits==1);
  put32(request,1);startupSendOk=false;r.receive(p,320);r.poll(320,true);
  assert(r.stats().retransmits==1 && r.stats().retransmitFailed==1);
  r.receive(p,321);r.poll(321,true);assert(r.stats().retransmitFailed==1);
  startupSendOk=true;put32(request,2);r.receive(p,700);
  assert(r.stats().cacheExpired==1 && r.stats().cacheMisses==1 && last().kind==Kind::Missing);
  auto count=r.stats().nacks;request[4]=1;r.receive(p,701);assert(r.stats().nacks==count);
  assert(r.stats().nackAgeMaxMs==660);startupSendOk=false;
 }

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
 auto previous=d.identity();d.poll(4500,true);auto hello=find(Kind::Hello);hello.kind=Kind::HelloAck;hello.flags=3;hello.id.server=99;
 d.receive(hello,4500);assert(d.takeFailure()&&d.identity().client!=previous.client&&!d.ready(4500));
 SessionClient e;e.init(88,send);handshake(e,0);e.setEventHandler(handleEvent);e.setEventReady(eventReady);
 assert(e.beginConversation());e.poll(1,true);ack(e,Kind::Start,Kind::StartAck,2);
 auto rid=e.identity();rid.response=7;uint8_t responseEvent[5]={3,0,0,0,1};size_t frames=sent.size();
 e.receive({Kind::Event,0,rid,1,responseEvent,5},3);assert(calls==1 && sent.size()==frames);
 e.receive({Kind::Event,0,rid,1,responseEvent,5},4);assert(calls==1 && sent.size()==frames);
 responseReady=true;e.poll(5,true);assert(last().kind==Kind::EventAck && last().body[3]==123);
 e.receive({Kind::Event,0,rid,1,responseEvent,5},6);assert(calls==1 && last().body[3]==123);
 assert(e.requestDownlink(10,3));assert(last().kind==Kind::DownlinkNack && last().id.response==7 && last().sequence==10 && last().body[5]==3);
 assert(!e.requestDownlink(10,0) && !e.requestDownlink(10,256));
 e.receive({Kind::AudioEndAck,0,rid,50,nullptr,0},6);
 assert(last().kind==Kind::AudioEndAck && last().sequence==50);
 auto wrongEnd=rid;wrongEnd.response=6;const size_t beforeWrongEnd=sent.size();
 e.receive({Kind::AudioEndAck,0,wrongEnd,50,nullptr,0},6);assert(sent.size()==beforeWrongEnd);
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
 assert(busy.responsive(100));
 assert(busy.responsive(4099) && !busy.responsive(4100) && busy.reachable(4100));
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
 assert(wrap.reachable(50)&&wrap.ready(50)&&wrap.responsive(50));
 SessionClient startup;startup.init(105,startupSend);startup.setObserver(startupTrace);
 startup.poll(100,false);assert(helloLogs==0);
 startup.poll(100,true);assert(helloLogs==1 && helloLocalOk==0);
 startupSendOk=true;startup.poll(600,true);assert(helloLogs==2 && helloLocalOk==1);
 auto reply=find(Kind::Hello);reply.kind=Kind::HelloAck;reply.id.server=2;reply.flags=3;
 startup.receive(reply,650);assert(connectedLogs==1 && helloElapsed==550 && helloAttempts==2);
 startup.poll(3600,true);assert(helloLogs==2); // Healthy HELLO keepalives are silent.
 startup.receive(find(Kind::Hello),3601);assert(connectedLogs==1);
 SessionClient oldServer;oldServer.init(110,send);oldServer.poll(0,true);
 auto oldReply=find(Kind::Hello);oldReply.kind=Kind::HelloAck;oldReply.id.server=2;oldReply.flags=1;
 oldServer.receive(oldReply,1);assert(oldServer.takeFailure() && !oldServer.ready(1));
 puts("PASS: v3 ACK retry, continuous PCM, response events/duplicate ACK, playback ACK, NACK once/cache miss, cancellation IDs, normal reuse, timeouts and HELLO diagnostics");
}
