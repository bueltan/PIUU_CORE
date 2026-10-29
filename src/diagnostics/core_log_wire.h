#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
// CMD! v1 / 0x10. Explicit little endian; CRC16-CCITT covers metadata + text.
namespace CoreLogWire {
constexpr uint8_t Command=0x10;
constexpr size_t Text=192, Metadata=17, Overhead=Metadata+2, MaxPayload=Overhead+Text;
inline void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i)); }
inline uint32_t get32(const uint8_t *p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
inline uint16_t crc(const uint8_t *p,size_t n) {
    uint16_t value=0xffff;
    while(n--) { value^=uint16_t(*p++)<<8;for(unsigned i=0;i<8;++i)value=uint16_t((value<<1)^((value&0x8000)?0x1021:0)); }
    return value;
}
inline size_t encode(uint8_t *p,size_t capacity,const uint8_t *text,size_t n,
                     uint32_t boot,uint32_t sequence,uint32_t ms,uint32_t dropped,uint8_t reset) {
    if(!p || !text || !n || n>Text || capacity<n+Overhead)return 0;
    put32(p,boot);put32(p+4,sequence);put32(p+8,ms);put32(p+12,dropped);p[16]=reset;
    memcpy(p+Metadata,text,n);const auto sum=crc(p,Metadata+n);
    p[Metadata+n]=uint8_t(sum);p[Metadata+n+1]=uint8_t(sum>>8);return n+Overhead;
}
inline bool valid(const uint8_t *p,size_t n) {
    return p && n>Overhead && n<=MaxPayload && crc(p,n-2)==(uint16_t(p[n-2])|uint16_t(p[n-1])<<8);
}
}
