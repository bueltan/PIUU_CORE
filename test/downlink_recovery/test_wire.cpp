#include "protocol/session_protocol.h"
#include "protocol/server_protocol.h"
#include <cassert>
#include <fstream>
#include <vector>
using namespace SessionProtocol;
int main(int argc,char **argv) {
    assert(argc==3);
    const uint8_t body[]={0,0,0,7,0,3};uint8_t out[MAX_PACKET];
    Packet request{Kind::DownlinkNack,0,{1,2,3,4},7,body,6};
    const size_t size=encode(out,sizeof out,request);assert(size==44);
    if(argv[1][0]=='w') { std::ofstream(argv[2],std::ios::binary).write(reinterpret_cast<char*>(out),size);return 0; }
    std::ifstream file(argv[2],std::ios::binary);std::vector<uint8_t> input((std::istreambuf_iterator<char>(file)),{});
    Packet end{};assert(decode(input.data(),input.size(),end));
    ServerProtocol::OpusPacket inner{};assert(ServerProtocol::decodeOpus(end.body,end.size,inner));
    assert(end.kind==Kind::AudioEnd && sameTurn(end.id,request.id) && end.id.response==4);
    assert(inner.end && inner.sequence==9 && end.sequence==9);
}
