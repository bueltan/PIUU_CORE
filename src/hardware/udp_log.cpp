#if PIUU_UDP_LOG
#include "hardware/udp_log.h"
#include "diagnostics/log_wire.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#include <esp_heap_caps.h>
namespace {
struct Chunk { uint32_t ms; uint16_t size; uint8_t data[LogWire::Payload]; };
QueueHandle_t queue=nullptr;
std::atomic<bool> enabled{false};
std::atomic<uint32_t> dropped{0};
constexpr uint32_t LeaseMs=10000;
void worker(void *) {
    WiFiUDP socket; // Never share the audio socket or its mutex.
    const uint32_t boot=esp_random();
    uint32_t sequence=0,lease=0,lastStatus=0;
    bool bound=false,subscribed=false;
    IPAddress collector;uint16_t port=0;
    uint8_t packet[LogWire::Header+LogWire::Payload];
    for(;;) {
        if(WiFi.status()!=WL_CONNECTED) {
            if(bound) socket.stop();
            bound=subscribed=false;
            vTaskDelay(pdMS_TO_TICKS(250));continue;
        }
        if(!bound) {
            bound=socket.begin(LogWire::Port)==1;
            if(!bound) { vTaskDelay(pdMS_TO_TICKS(1000));continue; }
        }
        const uint32_t now=millis();
        if(subscribed && uint32_t(now-lease)>=LeaseMs) subscribed=false;
        const int n=socket.parsePacket();
        if(n>0) {
            const IPAddress from=socket.remoteIP();const uint16_t sourcePort=socket.remotePort();
            char request[sizeof(LogWire::Subscribe)]={};
            const int read=socket.read(reinterpret_cast<uint8_t*>(request),sizeof(request));
            // Local LAN diagnostics only; one collector owns the renewable lease.
            const bool local=(uint32_t(from)&uint32_t(WiFi.subnetMask()))==
                             (uint32_t(WiFi.localIP())&uint32_t(WiFi.subnetMask()));
            if(local && n==int(sizeof(LogWire::Subscribe)-1) && read==n &&
               !memcmp(request,LogWire::Subscribe,n) &&
               (!subscribed || (from==collector && sourcePort==port))) {
                collector=from;port=sourcePort;subscribed=true;lease=now;
            }
            socket.flush();
        }
        if(subscribed) {
            Chunk chunk{};
            const bool status=uint32_t(now-lastStatus)>=5000 || sequence==0;
            if(status) {
                lastStatus=now;chunk.ms=now;
                chunk.size=snprintf(reinterpret_cast<char*>(chunk.data),sizeof(chunk.data),
                    "\n[UDP_LOG] heap=%u internal=%u stack_free_bytes=%u dropped_bytes=%lu\n",
                    unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
                    unsigned(uxTaskGetStackHighWaterMark(nullptr)),(unsigned long)dropped.load());
            }
            if(status || xQueueReceive(queue,&chunk,0)==pdTRUE) {
                const size_t size=LogWire::encode(packet,sizeof(packet),chunk.data,chunk.size,
                    boot,++sequence,dropped.load(),chunk.ms);
                bool ok=socket.beginPacket(collector,port)==1;
                if(ok) { const size_t written=socket.write(packet,size);const bool sent=socket.endPacket()==1;ok=written==size && sent; }
                if(!ok) dropped.fetch_add(chunk.size);
            }
        }
        // At most 25 datagrams/s (5.4 kB/s UDP payload including headers).
        // No retransmission or application ACK; socket work stays off audio tasks.
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
}
UdpAppLog &udpAppLog() { static UdpAppLog log;return log; }
size_t UdpAppLog::write(const uint8_t *data,size_t size) {
    if(!enabled.load()) return size;
    // Bound each producer call, even for an accidentally huge diagnostic.
    const size_t limit=size>1024?1024:size;
    size_t offset=0;
    while(offset<limit) {
        Chunk chunk{};chunk.ms=millis();
        chunk.size=uint16_t(limit-offset>LogWire::Payload?LogWire::Payload:limit-offset);
        memcpy(chunk.data,data+offset,chunk.size);
        if(xQueueSend(queue,&chunk,0)!=pdTRUE) {
            Chunk old{};
            if(xQueueReceive(queue,&old,0)==pdTRUE) dropped.fetch_add(old.size);
            if(xQueueSend(queue,&chunk,0)!=pdTRUE) dropped.fetch_add(chunk.size);
        }
        offset+=chunk.size;
    }
    if(size>limit) dropped.fetch_add(size-limit);
    return size;
}
void udpLogBegin() {
    if(queue) return;
    queue=xQueueCreate(4,sizeof(Chunk));
    if(!queue) return;
    enabled.store(true);
    if(xTaskCreatePinnedToCore(worker,"udp-log",4096,nullptr,1,nullptr,0)!=pdPASS)
        enabled.store(false);
}
#endif
