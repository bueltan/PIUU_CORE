#include "protocol/session_protocol.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>
using namespace SessionProtocol;
int main(int argc,char **argv) {
    const uint8_t body[]={1,2,3};
    Packet p{Kind::Pcm,0,{0x0102030405060708ULL,0x1112131415161718ULL,0x21222324,0x31323334},0x41424344,body,3};
    uint8_t bytes[MAX_PACKET]{};
    assert(encode(bytes,sizeof(bytes),p)==41);
    const uint8_t header[]={0x50,0x49,0x55,0x33,3,9,0,0,1,2,3,4,5,6,7,8,17,18,19,20,21,22,23,24,
        33,34,35,36,49,50,51,52,65,66,67,68,0,3};
    assert(!memcmp(bytes,header,HEADER_SIZE));
    Packet decoded{};assert(decode(bytes,41,decoded));
    assert(sameTurn(p.id,decoded.id) && decoded.sequence==p.sequence && decoded.size==3);
    assert(!memcmp(decoded.body,body,3));
    for(size_t n=0;n<41;++n) assert(!decode(bytes,n,decoded));
    bytes[4]=1;assert(!decode(bytes,41,decoded));bytes[4]=3;
    bytes[5]=22;assert(!decode(bytes,41,decoded));bytes[5]=9;
    bytes[37]=2;assert(!decode(bytes,41,decoded));bytes[37]=3;
    auto other=p.id;++other.turn;assert(sameSession(p.id,other) && !sameTurn(p.id,other));
    ++other.server;assert(!sameSession(p.id,other));
    assert(!encode(bytes,40,p));assert(!decode(nullptr,41,decoded));
    if(argc>1) std::ofstream(argv[1],std::ios::binary).write(reinterpret_cast<const char*>(bytes),41);
    if(argc>2) {
        std::ifstream file(argv[2],std::ios::binary);
        std::vector<uint8_t> input((std::istreambuf_iterator<char>(file)),{});
        assert(decode(input.data(),input.size(),decoded));
        assert(decoded.kind==Kind::Nack && decoded.sequence==7 && decoded.id.turn==9);
        assert(decoded.size==6 && decoded.body[5]==3);
    }
}
