#if PIUU_EARS_LOG
#include "hardware/ears_log.h"
#include "diagnostics/core_log_wire.h"
#include "diagnostics/wifi_history.h"
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>
namespace {
struct Chunk { uint32_t ms;uint16_t size;uint8_t data[CoreLogWire::Text]; };
Chunk queue[4];
size_t head=0,count=0;
uint32_t dropped=0,boot=0;
bool enabled=false;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
}
EarsAppLog &earsAppLog() { static EarsAppLog log;return log; }
void earsLogBegin() {
    // Before producers and UART worker start; fixed storage, no task/socket/heap.
    boot=esp_random();enabled=true;
}
size_t EarsAppLog::write(const uint8_t *data,size_t size) {
    if(!enabled || !data)return size;
    const size_t limit=size>1024?1024:size;
    const uint32_t now=millis();
    for(size_t offset=0;offset<limit;) {
        const size_t n=limit-offset>CoreLogWire::Text?CoreLogWire::Text:limit-offset;
        portENTER_CRITICAL(&mux);
        if(count==4) { dropped+=queue[head].size;head=(head+1)%4;--count; }
        auto &chunk=queue[(head+count)%4];chunk.ms=now;chunk.size=n;
        memcpy(chunk.data,data+offset,n);++count;
        portEXIT_CRITICAL(&mux);
        offset+=n;
    }
    if(size>limit) { portENTER_CRITICAL(&mux);dropped+=size-limit;portEXIT_CRITICAL(&mux); }
    return size;
}
void earsLogPoll(HardwareSerial &serial) {
    static uint32_t sequence=0,lastSend=0,lastStatus=0;
    static uint64_t wifiCursor=0;
    const uint32_t now=millis();
    // Reserve room for a command, and cap at 25 frames/s (4.8 kB/s text).
    if(!enabled || uint32_t(now-lastSend)<40 || serial.availableForWrite()<int(8+CoreLogWire::MaxPayload+8))return;
    Chunk chunk{};uint32_t lost;
    portENTER_CRITICAL(&mux);lost=dropped;portEXIT_CRITICAL(&mux);
    if(!sequence || uint32_t(now-lastStatus)>=5000) {
        chunk.ms=now;lastStatus=now;
        chunk.size=snprintf(reinterpret_cast<char*>(chunk.data),sizeof(chunk.data),
            "[CORE_LOG] build=%s %s heap=%u internal=%u largest=%u uart_stack=%u\n",
            __DATE__,__TIME__,unsigned(ESP.getFreeHeap()),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        if(chunk.size>=sizeof(chunk.data))chunk.size=sizeof(chunk.data)-1;
    } else if(WifiDiagnostics::next(wifiCursor,reinterpret_cast<char*>(chunk.data),sizeof(chunk.data),chunk.ms)) {
        chunk.size=strlen(reinterpret_cast<char*>(chunk.data));
    } else {
        portENTER_CRITICAL(&mux);
        if(count) { chunk=queue[head];head=(head+1)%4;--count; }
        portEXIT_CRITICAL(&mux);
        if(!chunk.size)return;
    }
    uint8_t frame[8+CoreLogWire::MaxPayload];
    memcpy(frame,"CMD!",4);frame[4]=1;frame[5]=CoreLogWire::Command;
    const size_t n=CoreLogWire::encode(frame+8,sizeof(frame)-8,chunk.data,chunk.size,
        boot,++sequence,chunk.ms,lost,uint8_t(esp_reset_reason()));
    frame[6]=uint8_t(n);frame[7]=uint8_t(n>>8);
    // This worker is the only UART writer; availableForWrite was checked above.
    if(serial.write(frame,n+8)!=n+8) {
        portENTER_CRITICAL(&mux);dropped+=chunk.size;portEXIT_CRITICAL(&mux);
    }
    lastSend=now;
}
#endif
