#include "protocol/server_protocol.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include <fstream>
using namespace ServerProtocol;
int main(int argc, char **argv) {
    int16_t pcm[320]={123,-456};
    uint8_t out[PCM_PACKET_MAX]={};
    assert(encodePcm(out,sizeof(out),0x12345678,0x87654321,pcm,320,false)==658);
    const uint8_t golden[]={ 'P','C','M','!',1,0,0x12,0x34,0x56,0x78,0x87,0x65,0x43,0x21,1,64,2,128 };
    assert(!memcmp(out,golden,sizeof(golden)) && !memcmp(out+18,pcm,640));
    if(argc>1) std::ofstream(argv[1],std::ios::binary).write(reinterpret_cast<const char *>(out),658);
    assert(!encodePcm(out,657,0,0,pcm,320,false));
    assert(!encodePcm(nullptr,658,0,0,pcm,320,false));
    assert(!encodePcm(out,658,0,0,nullptr,320,false));
    assert(!encodePcm(out,658,0,0,pcm,319,false));
    assert(!encodePcm(out,658,0,0,pcm,0,true));
    assert(encodePcm(out,658,0x12345678,0x87654321,nullptr,0,true)==18);
    assert(out[5]==1 && out[14]==0 && out[16]==0);
    if(argc>2) std::ofstream(argv[2],std::ios::binary).write(reinterpret_cast<const char *>(out),18);
    for(Control c:{Control::Ping,Control::Pong,Control::Commit}) {
        const auto message=controlMessage(c);
        assert(parseControl(message.data,message.size)==c);
        assert(parseControl(message.data,message.size-1)==Control::Unknown);
    }
    assert(parseControl(nullptr,4)==Control::Unknown);
    assert(parseControl(reinterpret_cast<const uint8_t *>("PONG!"),5)==Control::Unknown);
    std::vector<uint8_t> opus={'O','P','U','S',1,0,0x12,0x34,0x56,0x78,0x87,0x65,0x43,0x21,1,64,0,3,11,22,33};
    OpusPacket packet{};
    assert(decodeOpus(opus.data(),opus.size(),packet));
    assert(packet.sequence==0x12345678 && packet.ptsSamples==0x87654321 && packet.frameSamples==320);
    assert(packet.payload==opus.data()+18 && packet.payloadBytes==3 && !packet.end);
    for(size_t n=0;n<opus.size();++n) assert(!decodeOpus(opus.data(),n,packet));
    auto bad=opus;bad[0]='X';assert(!decodeOpus(bad.data(),bad.size(),packet));
    bad=opus;bad[4]=2;assert(!decodeOpus(bad.data(),bad.size(),packet));
    bad=opus;bad[15]=63;assert(!decodeOpus(bad.data(),bad.size(),packet));
    bad=opus;bad[5]=1;assert(!decodeOpus(bad.data(),bad.size(),packet));
    bad=opus;bad[16]=1;bad[17]=45;bad.resize(18+301);assert(!decodeOpus(bad.data(),bad.size(),packet));
    bad[17]=44;bad.resize(18+300);assert(decodeOpus(bad.data(),bad.size(),packet));
    bad=opus;bad.resize(18);bad[5]=1;bad[14]=bad[15]=bad[16]=bad[17]=0;
    assert(decodeOpus(bad.data(),bad.size(),packet) && packet.end);
    assert(!decodeOpus(nullptr,18,packet));
    // Decoder consumes a packet produced by the actual Python bridge class.
    if(argc>3) {
        std::ifstream file(argv[3],std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),{});
        assert(decodeOpus(data.data(),data.size(),packet));
        assert(packet.sequence==9 && packet.ptsSamples==2880 && packet.payloadBytes==3);
    }
    puts("PASS: server protocol wire bytes, controls and malformed packets");
}
