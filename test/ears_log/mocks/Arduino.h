#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
uint32_t millis();
class Print {
public:
 virtual ~Print()=default;
 virtual size_t write(uint8_t)=0;
 virtual size_t write(const uint8_t*,size_t)=0;
};
class HardwareSerial {
public:
 int space=256;
 std::vector<uint8_t> bytes;
 int availableForWrite() { return space; }
 size_t write(const uint8_t *p,size_t n) { if(int(n)>space)return 0;bytes.insert(bytes.end(),p,p+n);return n; }
};
struct Esp { unsigned getFreeHeap() { return 98765; } };
extern Esp ESP;
