#pragma once
#include <Arduino.h>
class UdpAppLog : public Print {
public:
    size_t write(uint8_t b) override { return write(&b,1); }
    size_t write(const uint8_t *,size_t) override;
};
UdpAppLog &udpAppLog();
void udpLogBegin();
