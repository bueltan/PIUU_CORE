#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstdio>
#include <string>
uint32_t millis();
struct Logger {
    std::string output;
    void begin(unsigned long) {}
    template<class... Args> void printf(const char *format,Args... args) {
        char line[1024];snprintf(line,sizeof(line),format,args...);output+=line;
    }
};
extern Logger Serial;
