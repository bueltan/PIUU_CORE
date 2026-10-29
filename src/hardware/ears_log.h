#pragma once
#include <Arduino.h>
class EarsAppLog : public Print {
public:
    size_t write(uint8_t byte) override { return write(&byte,1); }
    size_t write(const uint8_t *,size_t) override;
};
EarsAppLog &earsAppLog();
void earsLogBegin();
// Called only by the UART owner after RX/commands. Never writes UART elsewhere.
void earsLogPoll(HardwareSerial &serial);
