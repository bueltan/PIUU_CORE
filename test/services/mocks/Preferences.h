#pragma once
#include "Arduino.h"
#include <map>
struct FakeNvs {
    std::map<std::string,String> strings;
    std::map<std::string,int> integers;
    bool available=true;
    bool failWrites=false;
    int opens=0;
};
extern FakeNvs fakeNvs;
class Preferences {
    bool opened=false,readonly=false;
public:
    bool begin(const char *name,bool readOnly) {
        if(std::string(name)!="handwalle" || !fakeNvs.available) return false;
        opened=true;readonly=readOnly;++fakeNvs.opens;return true;
    }
    void end() { if(opened) --fakeNvs.opens;opened=false; }
    String getString(const char *key,const char *fallback="") {
        auto found=fakeNvs.strings.find(key);return found==fakeNvs.strings.end() ? String(fallback) : found->second;
    }
    int getInt(const char *key,int fallback=0) { auto found=fakeNvs.integers.find(key);return found==fakeNvs.integers.end() ? fallback : found->second; }
    bool isKey(const char *key) { return fakeNvs.strings.count(key) || fakeNvs.integers.count(key); }
    size_t putString(const char *key,const String &value) {
        if(!opened || readonly || fakeNvs.failWrites) return 0;
        fakeNvs.strings[key]=value;return value.length();
    }
    size_t putInt(const char *key,int value) {
        if(!opened || readonly || fakeNvs.failWrites) return 0;
        fakeNvs.integers[key]=value;return sizeof(int32_t);
    }
    bool remove(const char *key) {
        if(!opened || readonly || fakeNvs.failWrites) return false;
        return fakeNvs.strings.erase(key) || fakeNvs.integers.erase(key);
    }
};
