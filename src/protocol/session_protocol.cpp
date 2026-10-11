#include "protocol/session_protocol.h"
#include <cstring>
namespace SessionProtocol {
namespace {
uint64_t get(const uint8_t *p,size_t n) { uint64_t v=0;while(n--) v=(v<<8)|*p++;return v; }
void put(uint8_t *p,uint64_t v,size_t n) { while(n) { p[--n]=uint8_t(v);v>>=8; } }
}
size_t encode(uint8_t *out,size_t capacity,const Packet &p) {
    const uint8_t kind=uint8_t(p.kind);
    if (!out || kind<1 || kind>21 || p.size>MAX_BODY || (p.size && !p.body) || capacity<HEADER_SIZE+p.size) return 0;
    memcpy(out,"PIU3",4);out[4]=VERSION;out[5]=kind;put(out+6,p.flags,2);
    put(out+8,p.id.client,8);put(out+16,p.id.server,8);put(out+24,p.id.turn,4);put(out+28,p.id.response,4);
    put(out+32,p.sequence,4);put(out+36,p.size,2);
    if (p.size) memcpy(out+HEADER_SIZE,p.body,p.size);
    return HEADER_SIZE+p.size;
}
bool decode(const uint8_t *data,size_t size,Packet &p) {
    if (!data || size<HEADER_SIZE || memcmp(data,"PIU3",4) || data[4]!=VERSION || data[5]<1 || data[5]>21) return false;
    const uint16_t bytes=uint16_t(get(data+36,2));
    if (bytes>MAX_BODY || size!=HEADER_SIZE+bytes) return false;
    p={Kind(data[5]),uint16_t(get(data+6,2)),{get(data+8,8),get(data+16,8),uint32_t(get(data+24,4)),uint32_t(get(data+28,4))},
       uint32_t(get(data+32,4)),data+HEADER_SIZE,bytes};
    return true;
}
bool sameSession(const Identity &a,const Identity &b) { return a.client==b.client && a.server==b.server; }
bool sameTurn(const Identity &a,const Identity &b) { return sameSession(a,b) && a.turn==b.turn; }
}
