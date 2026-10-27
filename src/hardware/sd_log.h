#pragma once
#include <Arduino.h>
// Producers only enqueue bounded chunks; all filesystem access belongs to worker.
class SdAppLog : public Print {
public:
    size_t write(uint8_t byte) override { return write(&byte,1); }
    size_t write(const uint8_t *data,size_t size) override;
};
SdAppLog &sdAppLog();
void sdLogBegin();
