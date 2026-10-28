#if PIUU_SD_LOG
#include "hardware/sd_log.h"
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#include <algorithm>
#include "diagnostics/heap_snapshot.h"

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
constexpr size_t CHUNK=192, QUEUE_ITEMS=8, MAX_FILE=1024*1024;
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
    // Hold a small internal-RAM reserve through mount/open, then return it to
    // the running system. This is headroom, not a guarantee for future peaks.
    void *reserve=heap_caps_malloc(8192,MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(!reserve) {
        enabled.store(false);
        xQueueReset(queue);
        vTaskDelete(nullptr);
        return;
    }
    const auto beforeMount=HeapSnapshot::capture();
    bus.begin(PIUU_SD_SCK,PIUU_SD_MISO,PIUU_SD_MOSI,PIUU_SD_CS);
    if(SD.begin(PIUU_SD_CS,bus,4000000,"/sd",1) && openLog()) {
        if(file.size()>=MAX_FILE && !rotate()) goto done;
        file.printf("\n[SD_LOG] boot_ms=%lu queue_bytes=%u file_limit=%u audio_ready=1 reserve_bytes=8192\n",
            (unsigned long)millis(),unsigned(CHUNK*QUEUE_ITEMS),unsigned(MAX_FILE));
        beforeMount.print(file,"sd_before_mount");
        HeapSnapshot::capture().print(file,"sd_after_open");
        heap_caps_free(reserve);reserve=nullptr;
        HeapSnapshot::capture().print(file,"sd_reserve_released");
        file.flush();
        uint32_t lastStats=millis();
        uint32_t maxWriteUs=0, maxFlushUs=0;
        uint32_t lastFlush=millis();
        Chunk chunk{};
        for(;;) {
            if(xQueueReceive(queue,&chunk,pdMS_TO_TICKS(100))==pdTRUE) {
                if(file.size()+chunk.size>MAX_FILE && !rotate()) break;
                const uint32_t start=micros();
                if(file.write(chunk.data,chunk.size)!=chunk.size) break;
                maxWriteUs=std::max(maxWriteUs,uint32_t(micros()-start));
            }
            if(uint32_t(millis()-lastFlush)>=1000) {
                const auto lost=dropped.exchange(0);
                if(lost) file.printf("\n[SD_LOG] dropped_bytes=%lu\n",(unsigned long)lost);
                const uint32_t start=micros();
                file.flush();lastFlush=millis();
                maxFlushUs=std::max(maxFlushUs,uint32_t(micros()-start));
            }
            if(uint32_t(millis()-lastStats)>=10000) {
                file.printf("[SD_LOG] stack_free_bytes=%u max_write_us=%lu max_flush_us=%lu queued=%u\n",
                    unsigned(uxTaskGetStackHighWaterMark(nullptr)),
                    (unsigned long)maxWriteUs,(unsigned long)maxFlushUs,
                    unsigned(uxQueueMessagesWaiting(queue)));
                HeapSnapshot::capture().print(file,"sd_runtime");
                maxWriteUs=maxFlushUs=0;lastStats=millis();
            }
        }
    }
done:
    enabled.store(false); // Missing/full/removed card never blocks audio producers.
    file.close();SD.end();bus.end();
    heap_caps_free(reserve);
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
    enabled.store(true); // Bounded early capture only: no SD/task allocation yet.
}
void sdLogStartStorage() {
    static bool attempted=false; // Called by the application loop after startup.
    if(attempted || !queue) return;
    attempted=true;
    if(xTaskCreatePinnedToCore(worker,"sd-log",4096,nullptr,1,nullptr,0)!=pdPASS)
        enabled.store(false);
}
#endif
