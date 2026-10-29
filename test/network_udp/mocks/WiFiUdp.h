#pragma once
#include <stdint.h>
#include <stddef.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>
struct UdpMock {
    int begun=1,ended=1,readSize=-1;
    size_t written=4;
    unsigned writes=0,ends=0,cleared=0;
    std::deque<std::vector<uint8_t>> packets;
    std::vector<uint8_t> buffered;
    size_t cursor=0;
    void (*onParse)()=nullptr;
    void (*onEnd)()=nullptr;
};
extern UdpMock udpMock;
class WiFiUDP {
public:
    int begin(uint16_t) { clear();return 1; }
    int beginPacket(const char*,uint16_t) { return udpMock.begun; }
    size_t write(const uint8_t*,size_t) { ++udpMock.writes;return udpMock.written; }
    int endPacket() { ++udpMock.ends;if(udpMock.onEnd)udpMock.onEnd();return udpMock.ended; }
    // Arduino NetworkUDP returns zero while the previous datagram is buffered.
    int parsePacket() {
        if(udpMock.onParse)udpMock.onParse();
        if(available() || udpMock.packets.empty()) return 0;
        udpMock.buffered=udpMock.packets.front();udpMock.packets.pop_front();udpMock.cursor=0;
        return available();
    }
    int available() { return int(udpMock.buffered.size()-udpMock.cursor); }
    int read(uint8_t *out,size_t capacity) {
        size_t size=std::min(capacity,size_t(available()));
        if(udpMock.readSize>=0) size=std::min(size,size_t(udpMock.readSize));
        if(size) memcpy(out,udpMock.buffered.data()+udpMock.cursor,size);
        udpMock.cursor+=size;
        if(!available()) { udpMock.buffered.clear();udpMock.cursor=0; }
        return int(size);
    }
    void clear() { udpMock.buffered.clear();udpMock.cursor=0;++udpMock.cleared; }
};
