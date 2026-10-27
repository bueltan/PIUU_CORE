#if PIUU_SD_LOG
#include "hardware/sd_log.h"
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>

#ifndef PIUU_SD_CS
#define PIUU_SD_CS 5
#endif
#ifndef PIUU_SD_SCK
#define PIUU_SD_SCK 18
#endif
#ifndef PIUU_SD_MISO
#define PIUU_SD_MISO 19
#endif
#ifndef PIUU_SD_MOSI
#define PIUU_SD_MOSI 23
#endif
namespace {
constexpr size_t CHUNK=192, QUEUE_ITEMS=16, MAX_FILE=1024*1024;
struct Chunk { uint16_t size; uint8_t data[CHUNK]; };
QueueHandle_t queue=nullptr;
std::atomic<bool> enabled{false};
std::atomic<uint32_t> dropped{0};
SPIClass bus(VSPI); // TFT owns HSPI; touch is bit-banged on separate pins.
File file;
bool openLog() {
    file=SD.open("/piuu-core.log",FILE_APPEND);
    return bool(file);
}
bool rotate() {
    file.close();
    if(SD.exists("/piuu-core.previous.log") && !SD.remove("/piuu-core.previous.log")) return false;
    if(!SD.rename("/piuu-core.log","/piuu-core.previous.log")) return false;
    return openLog();
}
void worker(void *) {
    bus.begin(PIUU_SD_SCK,PIUU_SD_MISO,PIUU_SD_MOSI,PIUU_SD_CS);
    if(SD.begin(PIUU_SD_CS,bus,4000000) && openLog()) {
        if(file.size()>=MAX_FILE && !rotate()) goto done;
        file.printf("\n[SD_LOG] boot_ms=%lu queue_bytes=%u file_limit=%u\n",
            (unsigned long)millis(),unsigned(CHUNK*QUEUE_ITEMS),unsigned(MAX_FILE));
        file.flush();
        uint32_t lastFlush=millis();
        Chunk chunk{};
        for(;;) {
            if(xQueueReceive(queue,&chunk,pdMS_TO_TICKS(100))==pdTRUE) {
                if(file.size()+chunk.size>MAX_FILE && !rotate()) break;
                if(file.write(chunk.data,chunk.size)!=chunk.size) break;
            }
            if(uint32_t(millis()-lastFlush)>=1000) {
                const auto lost=dropped.exchange(0);
                if(lost) file.printf("\n[SD_LOG] dropped_bytes=%lu\n",(unsigned long)lost);
                file.flush();lastFlush=millis();
            }
        }
    }
done:
    enabled.store(false); // Missing/full/removed card never blocks audio producers.
    file.close();SD.end();bus.end();
    xQueueReset(queue); // Keep allocation valid for any producer already in write().
    vTaskDelete(nullptr);
}
}
SdAppLog &sdAppLog() { static SdAppLog log;return log; }
size_t SdAppLog::write(const uint8_t *data,size_t size) {
    if(!enabled.load()) return size;
    size_t offset=0;
    while(offset<size) {
        Chunk chunk{};
        chunk.size=uint16_t((size-offset)>CHUNK?CHUNK:(size-offset));
        memcpy(chunk.data,data+offset,chunk.size);
        if(xQueueSend(queue,&chunk,0)!=pdTRUE) dropped.fetch_add(chunk.size);
        offset+=chunk.size;
    }
    return size; // Best effort: Print callers must never retry/block on storage.
}
void sdLogBegin() {
    if(queue) return;
    queue=xQueueCreate(QUEUE_ITEMS,sizeof(Chunk));
    if(!queue) return;
    enabled.store(true); // Retain early BOOT messages while card initializes.
    if(xTaskCreatePinnedToCore(worker,"sd-log",4096,nullptr,1,nullptr,0)!=pdPASS)
        enabled.store(false);
}
#endif
