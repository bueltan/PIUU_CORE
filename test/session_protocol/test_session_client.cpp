#include "network/session_client.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace SessionProtocol;
static std::vector<std::vector<uint8_t>> sent;
static bool send(const uint8_t*p,size_t n) {sent.emplace_back(p,p+n);return true;}
static Packet last() {Packet p{};assert(decode(sent.back().data(),sent.back().size(),p));return p;}
static Packet find(Kind k) {for(auto i=sent.rbegin();i!=sent.rend();++i){Packet p{};decode(i->data(),i->size(),p);if(p.kind==k)return p;}assert(false);return {};}
static void ack(SessionClient &c,Kind request,Kind reply,uint32_t now) {auto p=find(request);p.kind=reply;p.flags=1;c.receive(p,now);}
static void handshake(SessionClient&c,uint32_t now) {c.poll(now,true);auto p=find(Kind::Hello);p.kind=Kind::HelloAck;p.id.server=2;p.flags=1;c.receive(p,now);assert(c.ready(now));}
static void put32(uint8_t*p,uint32_t n) {for(int i=3;i>=0;--i){p[i]=n;n>>=8;}}
static void pcm(SessionClient&c,uint32_t seq,uint32_t now) {uint8_t b[658]={};memcpy(b,"PCM!",4);b[4]=1;put32(b+6,seq);put32(b+10,seq*320);assert(c.sendBody(b,sizeof b,now));}
int main(){
 SessionClient c;c.init(1,send);handshake(c,0);assert(c.beginTurn());auto first=c.identity();pcm(c,0,1);c.poll(1,true);assert(find(Kind::Start).id.turn==first.turn);
 auto start=find(Kind::Start);c.poll(151,true);assert(find(Kind::Start).sequence==start.sequence);ack(c,Kind::Start,Kind::StartAck,152);c.poll(152,true);assert(last().kind==Kind::Pcm);
 uint8_t body[6]={0,0,0,0,0,1};Packet nack{Kind::Nack,0,c.identity(),0,body,6};size_t n=sent.size();c.receive(nack,153);c.poll(153,true);assert(sent.size()==n+1&&last().kind==Kind::Pcm);n=sent.size();c.receive(nack,154);c.poll(154,true);assert(sent.size()==n);
 put32(body,99);c.receive(nack,154);assert(last().kind==Kind::Missing&&last().sequence==99);
 c.cancel(123);assert(c.identity().turn==0);assert(c.beginTurn());auto second=c.identity();assert(second.turn>first.turn);c.poll(155,true);assert(last().kind==Kind::Cancel&&last().id.turn==first.turn);ack(c,Kind::Cancel,Kind::CancelAck,156);c.poll(156,true);assert(last().kind==Kind::Start&&last().id.turn==second.turn);
 c.receive({Kind::Error,0,first,0,nullptr,0},157);assert(!c.takeFailure());ack(c,Kind::Start,Kind::StartAck,158);c.finishTurn();assert(c.beginTurn());
 c.poll(6001,true);assert(c.takeFailure()&&!c.ready(6001));
 SessionClient d;d.init(7,send);handshake(d,0);assert(d.beginTurn());for(int i=0;i<3;++i)pcm(d,i,1);assert(d.sendBody(reinterpret_cast<const uint8_t*>("COMMIT"),6,1));d.poll(1,true);ack(d,Kind::Start,Kind::StartAck,2);d.poll(2,true);assert(last().kind==Kind::Pcm&&last().sequence==1);d.poll(3,true);assert(last().kind==Kind::Pcm&&last().sequence==2);d.poll(4,true);assert(last().kind==Kind::Commit);ack(d,Kind::Commit,Kind::CommitAck,5);d.finishTurn();assert(d.beginTurn());
 d.poll(10,true);for(int i=1;i<=8;++i)d.poll(10+150*i,true);assert(d.takeFailure());
 auto retired=d.identity().client;d.reset();assert(d.identity().client!=retired);handshake(d,1500);
 auto previous=d.identity();d.poll(4500,true);auto hello=find(Kind::Hello);hello.kind=Kind::HelloAck;hello.flags=1;hello.id.server=99;
 d.receive(hello,4500);assert(d.takeFailure()&&d.identity().client!=previous.client&&!d.ready(4500));
 puts("PASS: v2 ACK retry, startup/COMMIT ordering, NACK once/cache miss, cancellation IDs, normal reuse and timeouts");
}
