#include "input/ears_link.h"
#include "diagnostics/config.h"
#include "config/ears_config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <cstring>
#include <type_traits>
namespace {
HardwareSerial &serial=Serial2; // Dedicated EARS UART; Serial remains the USB console.
SemaphoreHandle_t packetMutex=nullptr;
QueueHandle_t audioQueue=nullptr,eventQueue=nullptr;
TaskHandle_t worker=nullptr;
std::atomic<uint32_t> order{0},lastPacketMs{0},request{0};
std::atomic_bool seenPeer{false},remoteStreaming{false};
uint32_t localAckErrors=0; // worker-only
std::atomic<uint32_t> audioReceived{0},audioDropped{0},eventDropped{0},parseRejected{0},commandsSent{0};
#if PIUU_PERFORMANCE_DIAGNOSTICS
std::atomic<uint32_t> audioDepth{0},eventDepth{0},audioPeak{0},eventPeak{0};
// All queue mutations are serialized by packetMutex, so peaks cannot race.
void queued(std::atomic<uint32_t>& depth,std::atomic<uint32_t>& peak) {
    const uint32_t value=++depth;
    if (value>peak.load()) peak.store(value);
}
#endif
static_assert(std::is_trivially_copyable<EarsMessage>::value,"UART queues must own their data");
void receive(void*,const EarsProtocol::PacketView &packet) {
    if (packet.kind==EarsProtocol::Kind::Event && packet.code==uint8_t(EarsProtocol::Event::StreamState) &&
        (packet.size!=1 || packet.payload[0]>1)) { ++localAckErrors;return; }
    if (xSemaphoreTake(packetMutex,portMAX_DELAY)!=pdTRUE) return;
    const uint32_t serialOrder=order.fetch_add(1)+1;
    lastPacketMs.store(millis());seenPeer.store(true);
    if (packet.kind==EarsProtocol::Kind::Pcm) {
        EarsAudioFrame frame{};frame.order=serialOrder;frame.sequence=packet.sequence;
        // Explicit little endian conversion, independent of packed struct alignment.
        for (size_t i=0;i<PCM_FRAME_SAMPLES;++i) {
            const uint16_t value=uint16_t(packet.payload[2*i]) | uint16_t(packet.payload[2*i+1])<<8;
            memcpy(frame.samples+i,&value,sizeof(value));
        }
        ++audioReceived;
        if (xQueueSend(audioQueue,&frame,0)!=pdTRUE) ++audioDropped;
#if PIUU_PERFORMANCE_DIAGNOSTICS
        else queued(audioDepth,audioPeak);
#endif
    } else {
        if (packet.code==uint8_t(EarsProtocol::Event::StreamState)) {
            remoteStreaming.store(packet.payload[0]!=0);
            xSemaphoreGive(packetMutex);
            return; // Transport acknowledgement; never a conversation end event.
        }
        EarsEvent event{};event.order=serialOrder;event.code=packet.code;event.size=packet.size;
        if (packet.size) memcpy(event.payload,packet.payload,packet.size);
        if (xQueueSend(eventQueue,&event,0)!=pdTRUE) ++eventDropped;
#if PIUU_PERFORMANCE_DIAGNOSTICS
        else queued(eventDepth,eventPeak);
#endif
    }
    xSemaphoreGive(packetMutex);
}
EarsProtocol::Parser parser(receive,nullptr);
void run(void*) {
    uint32_t applied=UINT32_MAX,lastCommand=0;
    for (;;) {
        const uint32_t now=millis(),intent=request.load();
        if (intent!=applied || uint32_t(now-lastCommand)>=EarsConfig::COMMAND_REFRESH_MS) {
            uint8_t bytes[EarsProtocol::CONTROL_HEADER_BYTES];
            const auto command=(intent&1) ? EarsProtocol::Command::Start : EarsProtocol::Command::Stop;
            const size_t size=EarsProtocol::encodeCommand(bytes,sizeof(bytes),command);
            // TX buffer is larger than a command. Do not wait for a busy UART.
            if (serial.availableForWrite()>=int(size) && serial.write(bytes,size)==size) {
                applied=intent;lastCommand=now;++commandsSent;
            }
        }
        parser.expire(now,EarsConfig::RX_TIMEOUT_MS);
        for (size_t n=0;n<1024 && serial.available()>0;++n) {
            const int byte=serial.read();if (byte<0) break;
            parser.feed(uint8_t(byte),millis());
        }
        // Parser owns its counter; expose only an atomic snapshot to loop.
        parseRejected.store(parser.rejected()+localAckErrors);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
}

bool earsLinkBegin() {
    if (worker) return true;
    packetMutex=xSemaphoreCreateMutex();
    if (!packetMutex) return false;
    audioQueue=xQueueCreate(EarsConfig::AUDIO_QUEUE_FRAMES,sizeof(EarsAudioFrame));
    eventQueue=xQueueCreate(EarsConfig::EVENT_QUEUE_ITEMS,sizeof(EarsEvent));
    if (!audioQueue || !eventQueue) {
        if (audioQueue) vQueueDelete(audioQueue);
        if (eventQueue) vQueueDelete(eventQueue);
        audioQueue=eventQueue=nullptr;vSemaphoreDelete(packetMutex);packetMutex=nullptr;return false;
    }
    serial.end(); // Configure only the dedicated EARS UART.
    if (serial.setRxBufferSize(EarsConfig::RX_BUFFER_BYTES)!=EarsConfig::RX_BUFFER_BYTES ||
        serial.setTxBufferSize(EarsConfig::TX_BUFFER_BYTES)!=EarsConfig::TX_BUFFER_BYTES) {
        vQueueDelete(audioQueue);vQueueDelete(eventQueue);vSemaphoreDelete(packetMutex);
        audioQueue=eventQueue=nullptr;packetMutex=nullptr;return false;
    }
    serial.begin(EarsConfig::BAUD,SERIAL_8N1,EarsConfig::RX_PIN,EarsConfig::TX_PIN);
    if (!serial || xTaskCreatePinnedToCore(run,"earsUart",EarsConfig::WORKER_STACK_BYTES,nullptr,2,&worker,0)!=pdPASS) {
        serial.end();vQueueDelete(audioQueue);vQueueDelete(eventQueue);
        audioQueue=eventQueue=nullptr;worker=nullptr;vSemaphoreDelete(packetMutex);packetMutex=nullptr;return false;
    }
    return true;
}
bool earsLinkRequestStreaming(bool enabled) {
    if (!worker) return false;
    // One atomic value carries both desired state and revision. Repeated calls
    // explicitly request a resend without mixing state/revision across cores.
    const uint32_t old=request.load();request.store(((old+2)&~uint32_t(1)) | uint32_t(enabled));
    return true;
}
uint32_t earsLinkReceivedOrder() { return order.load(); }
bool earsLinkPeerOnline() {
    if (!seenPeer.load()) return false;
    const uint32_t received=lastPacketMs.load();
    return uint32_t(millis()-received)<EarsConfig::PEER_TIMEOUT_MS;
}
bool earsLinkTake(EarsMessage &message) {
    if (!audioQueue || !eventQueue || xSemaphoreTake(packetMutex,0)!=pdTRUE) return false;
    EarsAudioFrame audio{};EarsEvent event{};
    const bool haveAudio=xQueuePeek(audioQueue,&audio,0)==pdTRUE;
    const bool haveEvent=xQueuePeek(eventQueue,&event,0)==pdTRUE;
    // Merge independent queues by arrival order, including uint32 rollover.
    // A congested PCM queue cannot prevent control events from being queued.
    if (haveEvent && (!haveAudio || int32_t(event.order-audio.order)<0)) {
        message.kind=EarsProtocol::Kind::Event;
        const bool ok=xQueueReceive(eventQueue,&message.event,0)==pdTRUE;
#if PIUU_PERFORMANCE_DIAGNOSTICS
        if (ok) --eventDepth;
#endif
        xSemaphoreGive(packetMutex);return ok;
    }
    if (haveAudio) {
        message.kind=EarsProtocol::Kind::Pcm;
        const bool ok=xQueueReceive(audioQueue,&message.audio,0)==pdTRUE;
#if PIUU_PERFORMANCE_DIAGNOSTICS
        if (ok) --audioDepth;
#endif
        xSemaphoreGive(packetMutex);return ok;
    }
    xSemaphoreGive(packetMutex);return false;
}
EarsLinkStats earsLinkStats() {
    return {audioReceived.load(),audioDropped.load(),eventDropped.load(),parseRejected.load(),commandsSent.load(),remoteStreaming.load()};
}

EarsQueueStats earsLinkQueueStats() {
#if PIUU_PERFORMANCE_DIAGNOSTICS
    return {audioDepth.load(),audioPeak.load(),eventDepth.load(),eventPeak.load()};
#else
    return {0,0,0,0};
#endif
}
