#include "protocol/ears_protocol.h"
#include "audio_protocol.h"
#include "core_link.h"
#include "ears_state.h"
#include "control_protocol.h"
#include "Arduino.h"
#include <cassert>
#include <vector>
#include <cstdio>
SerialData serialData[2];Logger Serial;
static uint32_t now=1;
uint32_t millis() { return now; }
struct Received { EarsProtocol::Kind kind;uint32_t sequence;uint8_t code;std::vector<uint8_t> payload; };
static std::vector<Received> received;
static void receive(void*,const EarsProtocol::PacketView &p) {
    Received copy{p.kind,p.sequence,p.code,{}};
    if(p.size) copy.payload.assign(p.payload,p.payload+p.size);
    received.push_back(copy);
}
static void feed(EarsProtocol::Parser &parser,const std::vector<uint8_t> &p) { for(auto b:p) parser.feed(b,now); }
static std::vector<uint8_t> event(uint8_t code,std::vector<uint8_t> payload={},uint8_t version=1) {
    std::vector<uint8_t> p={'E','V','T','!',version,code,uint8_t(payload.size()),uint8_t(payload.size()>>8)};
    p.insert(p.end(),payload.begin(),payload.end());return p;
}
int main() {
    HardwareSerial wire(0);uint8_t pcm[PCM_FRAME_BYTES];
    for(size_t i=0;i<sizeof(pcm);++i) pcm[i]=uint8_t(i);
    memcpy(pcm+50,"PCM!EVT!CMD!",12); // Never interpret magic inside PCM.
    assert(sendAudioFrame(wire,0xf0e1d2c3,pcm,sizeof(pcm)));
    const auto frame=serialData[0].tx;serialData[0].tx.clear();
    assert(frame.size()==650);
    for(size_t split=0;split<=frame.size();++split) {
        EarsProtocol::Parser parser(receive,nullptr);received.clear();
        for(size_t i=0;i<split;++i) parser.feed(frame[i],now);
        assert(!parser.expire(now+1,100));
        for(size_t i=split;i<frame.size();++i) parser.feed(frame[i],now+1);
        assert(received.size()==1 && received[0].sequence==0xf0e1d2c3 && received[0].payload.size()==640);
        assert(!memcmp(received[0].payload.data(),pcm,640));
    }
    // Every split through magic/header/PCM must work with bulk driver reads.
    for(size_t split=0;split<=frame.size();++split) {
        EarsProtocol::Parser bulk(receive,nullptr);received.clear();
        bulk.feed(frame.data(),split,now);
        bulk.feed(frame.data()+split,frame.size()-split,now+1);
        assert(received.size()==1 && received[0].sequence==0xf0e1d2c3);
        assert(received[0].payload.size()==640 && !memcmp(received[0].payload.data(),pcm,640));
    }
    // Bulk and byte paths must agree across noise, discarded frames, embedded
    // magic and several events in one read.
    std::vector<uint8_t> stream={'x','P','C','P','E','V'};
    const auto append=[&](const std::vector<uint8_t> &part) { stream.insert(stream.end(),part.begin(),part.end()); };
    append(event(42,{'P','C','M','!','E','V','T','!'}));append(frame);
    append(event(1));append(event(1,{'E','V','T','!',1,1,0,0},2));
    append(event(1,std::vector<uint8_t>(33,'P')));
    auto zero=frame;zero.resize(10);zero[8]=zero[9]=0;append(zero);append(event(3));
    EarsProtocol::Parser reference(receive,nullptr);received.clear();feed(reference,stream);
    const auto expected=received;
    for(size_t chunk: {1u,2u,3u,7u,8u,9u,10u,31u,127u,256u,650u,4096u}) {
        EarsProtocol::Parser bulk(receive,nullptr);received.clear();
        for(size_t offset=0;offset<stream.size();) {
            const size_t count=stream.size()-offset<chunk ? stream.size()-offset : chunk;
            bulk.feed(stream.data()+offset,count,now);offset+=count;
        }
        assert(bulk.rejected()==reference.rejected() && received.size()==expected.size());
        for(size_t i=0;i<expected.size();++i) {
            assert(received[i].kind==expected[i].kind && received[i].sequence==expected[i].sequence);
            assert(received[i].code==expected[i].code && received[i].payload==expected[i].payload);
        }
    }
    EarsProtocol::Parser partial(receive,nullptr);received.clear();
    partial.feed(frame.data(),20,10);partial.feed(nullptr,0,1000);
    assert(partial.expire(110,100)); // Empty chunks cannot extend timeout.
    partial.feed(frame.data(),frame.size(),111);assert(received.size()==1);
    EarsProtocol::Parser parser(receive,nullptr);received.clear();
    feed(parser,{'x','P','C','P','E','V'});feed(parser,event(42,{'P','C','M','!','E','V','T','!'}));
    feed(parser,frame);feed(parser,event(1));assert(received.size()==3 && received[0].code==42);
    const auto count=received.size();
    feed(parser,event(1,{'E','V','T','!',1,1,0,0},2)); // Unsupported version; consume payload.
    feed(parser,event(1,std::vector<uint8_t>(33,'P')));
    auto bad=frame;bad.resize(10);bad[8]=0;bad[9]=0;feed(parser,bad);
    assert(received.size()==count && parser.rejected()==3);
    feed(parser,event(3));assert(received.size()==count+1);
    for(size_t i=0;i<20;++i) parser.feed(frame[i],now);
    assert(parser.expire(now+100,100));now+=101;
    feed(parser,frame);assert(received.back().kind==EarsProtocol::Kind::Pcm);
    parser.feed('P',UINT32_MAX-5);assert(parser.expire(100,100));
    // Actual CORE encoder -> actual EARS command handler -> actual ACK -> CORE parser.
    assert(coreLinkBegin());
    uint8_t command[8];assert(EarsProtocol::encodeCommand(command,sizeof(command),EarsProtocol::Command::Start)==8);
    serialData[0].rx.insert(serialData[0].rx.end(),command,command+8);coreLinkProcessRx();
    assert(earsIsStreamingToCore());feed(parser,serialData[0].tx);serialData[0].tx.clear();
    assert(received.back().code==0x80 && received.back().payload==std::vector<uint8_t>({1}));
    assert(EarsProtocol::encodeCommand(command,sizeof(command),EarsProtocol::Command::Stop)==8);
    serialData[0].rx.insert(serialData[0].rx.end(),command,command+8);coreLinkProcessRx();
    assert(!earsIsStreamingToCore());feed(parser,serialData[0].tx);serialData[0].tx.clear();
    assert(received.back().payload==std::vector<uint8_t>({0}));
    assert(!EarsProtocol::encodeCommand(command,7,EarsProtocol::Command::Start));
    assert(!EarsProtocol::encodeCommand(command,8,EarsProtocol::Command(99)));
    assert(coreLinkForwardEvent(42,pcm,32));feed(parser,serialData[0].tx);
    assert(received.back().code==42 && received.back().payload.size()==32);
    puts("PASS: mixed UART framing, fragmentation, resync, embedded magic and real EARS PCM/CMD/EVT interoperability");
}
