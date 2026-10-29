#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cstring>
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const std::string &s) : std::string(s) {}
    String(char c) : std::string(1,c) {}
    String substring(size_t start, size_t end = std::string::npos) const { return substr(start, end == std::string::npos ? end : end-start); }
    void remove(size_t start) { erase(start); }
    String(int n) : std::string(std::to_string(n)) {}
    void toCharArray(char *out, size_t size) const { if (size) { std::strncpy(out,c_str(),size-1); out[size-1]=0; } }
};
inline String operator+(const char *a, const String &b) { return String(a)+std::string(b); }
template<class T> T constrain(T value, T lo, T hi) { return std::max(lo,std::min(hi,value)); }
uint32_t millis();
#ifdef PIUU_TEST_CAPTURE_LOG
#include <cstdio>
struct Logger {
    std::string output;
    void begin(uint32_t) {}
    void println(const char *text) { output+=text;output+='\n'; }
    template<class... Args> void printf(const char *format, Args... args) {
        char buffer[2048];std::snprintf(buffer,sizeof(buffer),format,args...);output+=buffer;
    }
};
#else
struct Logger { void begin(uint32_t) {} void println(const char *) {} template<class... Args> void printf(const char *, Args...) {} };
#endif
#ifdef PIUU_TEST_SERIAL_UART
#include "hardware_serial.h"
extern HardwareSerial Serial;
extern HardwareSerial Serial2;
#else
extern Logger Serial;
#endif
