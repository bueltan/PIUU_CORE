#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
namespace LogWire {
constexpr uint16_t Port=12001;
constexpr size_t Payload=192, Header=24;
constexpr char Subscribe[]="PIUU_LOG_SUBSCRIBE_V1";
inline void put32(uint8_t *p,uint32_t n) { p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
inline size_t encode(uint8_t *out,size_t capacity,const uint8_t *data,size_t size,
                     uint32_t boot,uint32_t seq,uint32_t dropped,uint32_t ms) {
    if(!out || !data || !size || size>Payload || capacity<Header+size) return 0;
    memcpy(out,"PLG1",4);put32(out+4,boot);put32(out+8,seq);
    put32(out+12,dropped);put32(out+16,ms);
    out[20]=size>>8;out[21]=size;out[22]=out[23]=0;
    memcpy(out+Header,data,size);return Header+size;
}
}
