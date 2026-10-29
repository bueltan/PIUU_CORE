#include "diagnostics/wifi_history.h"
#include "diagnostics/wifi_history_buffer.h"
#include "WiFi.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
using namespace WifiDiagnostics;
FakeWifi WiFi;
uint32_t millis() { return 1234; }
int main() {
    History buffer;
    uint64_t cursor=0, skipped=0;
    Entry entry{};
    assert(!buffer.read(cursor,entry,skipped));
    for (uint32_t i=0;i<70;++i) buffer.push({i,Event::Attempt,int32_t(i),0});
    cursor=1;
    assert(buffer.read(cursor,entry,skipped) && skipped==6 && entry.ms==6);
    for (uint32_t i=7;i<70;++i) {
        assert(buffer.read(cursor,entry,skipped) && skipped==0 && entry.ms==i);
    }
    assert(!buffer.read(cursor,entry,skipped));
    cursor=0;assert(buffer.read(cursor,entry,skipped) && entry.ms==6);
    // Readers get complete records while producers overwrite the bounded ring.
    std::thread producer([&]{for(int i=0;i<10000;++i) buffer.push({uint32_t(i),Event::Attempt,i,uint32_t(i)});});
    cursor=0;
    for(int i=0;i<10000;++i) if(buffer.read(cursor,entry,skipped))
        assert(entry.ms==uint32_t(entry.detail));
    producer.join();

    char line[192]; uint32_t ms=0; cursor=0;
    begin();
#if PIUU_WIFI_DIAGNOSTICS
    assert(WiFi.callback);
    record(Event::Attempt,6);
    WiFiEventInfo_t info{};
    info.wifi_sta_disconnected.reason=202;
    WiFi.callback(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,info);
    info.got_ip.ip_info.ip.addr=0x2a01a8c0;
    WiFi.callback(ARDUINO_EVENT_WIFI_STA_GOT_IP,info);
    record(Event::Timeout,0,15000);
    assert(!next(cursor,line,8,ms) && cursor==0);
    assert(next(cursor,line,sizeof(line),ms) && ms==1234 && std::string(line).find("event=attempt")!=std::string::npos);
    assert(next(cursor,line,sizeof(line),ms) && std::string(line).find("reason=202")!=std::string::npos);
    assert(next(cursor,line,sizeof(line),ms) && std::string(line).find("ip=192.168.1.42")!=std::string::npos);
    assert(next(cursor,line,sizeof(line),ms) && std::string(line).find("elapsed_ms=15000")!=std::string::npos);
    assert(!next(cursor,line,sizeof(line),ms));
    cursor=0;assert(next(cursor,line,sizeof(line),ms)); // New subscriber replays.
#else
    assert(!WiFi.callback);
    record(Event::Attempt);
    assert(!next(cursor,line,sizeof(line),ms));
#endif
    puts("PASS: bounded WiFi history, overwrite detection, concurrent access, event replay and diagnostics toggle");
}
