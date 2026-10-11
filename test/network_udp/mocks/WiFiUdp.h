#pragma once
#include <stdint.h>
#include <stddef.h>
struct UdpMock {
    int begun=1,ended=1,readSize=4;
    size_t written=4;
    unsigned writes=0,ends=0;
    void (*onParse)()=nullptr;
};
extern UdpMock udpMock;
class WiFiUDP {
public:
    int begin(uint16_t) { return 1; }
    int beginPacket(const char*,uint16_t) { return udpMock.begun; }
    size_t write(const uint8_t*,size_t) { ++udpMock.writes;return udpMock.written; }
    int endPacket() { ++udpMock.ends;return udpMock.ended; }
    int parsePacket() { if(udpMock.onParse)udpMock.onParse();return 4; }
    int read(uint8_t*,size_t) { return udpMock.readSize; }
};
