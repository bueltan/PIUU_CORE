#pragma once
#include <cstddef>
#include <cstdint>
uint32_t millis();
struct TestSerial { void begin(uint32_t) {} void println(const char *) {} template<class... Args> void printf(const char *, Args...) {} };
extern TestSerial Serial;
