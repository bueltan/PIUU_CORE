#include "audio/visual_clock.h"
#include "diagnostics/config.h"
#include "diagnostics/rx_service.h"
#if PIUU_PLAYBACK_DIAGNOSTICS
#include "diagnostics/audio_progress.h"
static Diagnostics::AudioProgress audioProgress;
#define AUDIO_PHASE(value) audioProgress.enter(Diagnostics::AudioProgress::value,millis())
#else
#define AUDIO_PHASE(value) ((void)0)
#endif
#include "diagnostics/dac_trace.h"
#if PIUU_DAC_DIAGNOSTICS
static Diagnostics::DacTrace dacTrace;
#endif
#include "hardware/app_log.h"
/**
 * @file audio_downlink.cpp
 * @brief UDP Opus reception, sequence-based buffering and I2S playback. The jitter buffer
 * absorbs timing variation; FEC and PLC attempt loss recovery.
 */

#include "audio/audio_downlink.h"
#include "diagnostics/performance.h"
#include "diagnostics/metrics.h"
#include "diagnostics/downlink_trace.h"
#include "audio/playback_completion.h"
#include "audio/downlink_recovery.h"

#include <Arduino.h>

#include <opus.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "audio/audio_output.h"
#include "network/server_link.h"
#include "protocol/server_protocol.h"
#include <atomic>
#include "config/app_config.h"

// ============================================================
// Downlink Opus protocol
// ============================================================

// ============================================================
// Packet slot
// ============================================================
struct PacketSlot
{
    bool used;
    uint32_t seq;
    uint32_t pts_samples;
    uint16_t frame_samples;
    uint16_t payload_len;
    uint8_t flags;
    uint8_t payload[ServerProtocol::MAX_OPUS_PAYLOAD];
};

static PacketSlot jitter[JITTER_SLOTS];
static uint32_t jitterBuffered=0;
#if PIUU_PERFORMANCE_DIAGNOSTICS
static uint32_t jitterPeak=0;
#endif
#if PIUU_PERFORMANCE_DIAGNOSTICS || PIUU_NETWORK_DIAGNOSTICS
static Diagnostics::Rebuffer pauses;
#endif
#if PIUU_NETWORK_DIAGNOSTICS
static Diagnostics::DownlinkTrace downlinkTrace;
#endif

// ============================================================
// Playback state
// ============================================================
static bool playbackStarted = false;
static bool rebuffering = true;
static bool endSeen = false;
static bool resumeLogArmed = true;

static uint32_t expectedSeq = 0;
static uint32_t highestSeqSeen = 0;
static uint32_t endSeq = 0;
static uint32_t endReceivedMs=0;
static DownlinkRecovery recovery;
static uint32_t recoveryEpoch=0;
static uint32_t consecutiveMisses = 0;
static uint32_t lastPacketMillis = 0;

// stats
static std::atomic<uint32_t> receivedPackets{0};
static std::atomic<uint32_t> droppedPackets{0};
static std::atomic<uint32_t> duplicatePackets{0};
static std::atomic<uint32_t> latePackets{0};
static std::atomic<uint32_t> aheadDrops{0};
static std::atomic<uint32_t> missingPackets{0};
static std::atomic<uint32_t> decodedPackets{0};
static std::atomic<uint32_t> decodeErrors{0};
static std::atomic<uint32_t> malformedPackets{0};
static std::atomic<uint32_t> plcPackets{0};
static std::atomic<uint32_t> fecRecoveredPackets{0};
static std::atomic<uint32_t> hardRebuffers{0};

// ============================================================
// Decoder
// ============================================================
static OpusDecoder *opusDecoder = nullptr;
static int opusErr = 0;

static PacketSlot playbackPkt;
static int16_t pcmOut[MAX_DECODE_SAMPLES];
static int16_t silenceFrame[OPUS_FRAME_SAMPLES] = {0};

// ============================================================
// RTOS / sync
// ============================================================
static SemaphoreHandle_t jitterMutex = nullptr;
static std::atomic<uint32_t> requestedEpoch{0};
static std::atomic<uint32_t> processedEpoch{0};
static std::atomic_bool expectResponse{false};
static std::atomic<uint32_t> responseExpectedAt{0};
static std::atomic<uint32_t> submittedMediaSamples{0};
static std::atomic<uint32_t> visualRevision{0},visualConsumed{0},visualSubmitted{0},visualAt{0};
static VisualClock visualClock; // Playback worker only; publish an atomic snapshot.
static void publishVisualClock() {
    ++visualRevision;
    visualConsumed=visualClock.consumed;visualSubmitted=visualClock.submitted;visualAt=visualClock.atMs;
    ++visualRevision;
}
bool audioDownlinkVisualPosition(uint32_t &positionMs) {
    const uint32_t epoch=requestedEpoch.load();
    if(!epoch || processedEpoch.load()!=epoch)return false;
    for(unsigned i=0;i<2;++i) {
        const uint32_t revision=visualRevision.load();if(revision&1)continue;
        VisualClock snapshot{visualConsumed.load(),visualSubmitted.load(),visualAt.load()};
        if(revision!=visualRevision.load() || requestedEpoch.load()!=epoch || processedEpoch.load()!=epoch)continue;
        if(!snapshot.submitted)return false;
        positionMs=snapshot.samples(millis())/16;return true;
    }
    return false;
}
static uint32_t lastMediaWriteMs=0;
bool audioDownlinkEpochReady() { return processedEpoch.load()==requestedEpoch.load(); }
uint32_t audioDownlinkPlayedMs() {
    if(processedEpoch.load()!=requestedEpoch.load()) return 0;
    const uint32_t submitted=submittedMediaSamples.load();
    // Conservative DAC estimate: exclude the full 8x256-sample DMA capacity.
    return submitted>2048 ? (submitted-2048)/16 : 0;
}
static PlaybackEventSink eventSink = nullptr; // Immutable after workers start.

void audioDownlinkSetEventSink(PlaybackEventSink sink) { eventSink = sink; }
void audioDownlinkBeginTurn(uint32_t epoch) {
    expectResponse.store(false);
    requestedEpoch.store(epoch);
}
void audioDownlinkExpectResponse() { responseExpectedAt.store(millis());expectResponse.store(true); }


// ============================================================
// Helpers
// ============================================================
// ============================================================
// Jitter helpers
// ============================================================
/**
 * @brief Frees every jitter slot and clears its metadata. The caller must protect access with
 * jitterMutex.
 */
static void clearJitterNoLock()
{
    jitterBuffered=0;
#if PIUU_PERFORMANCE_DIAGNOSTICS
    jitterPeak=0;
#endif
#if PIUU_PERFORMANCE_DIAGNOSTICS || PIUU_NETWORK_DIAGNOSTICS
    pauses=Diagnostics::Rebuffer{};
#endif
#if PIUU_NETWORK_DIAGNOSTICS
    downlinkTrace=Diagnostics::DownlinkTrace{};
#endif
    for (int i = 0; i < JITTER_SLOTS; ++i)
    {
        jitter[i].used = false;
        jitter[i].seq = 0;
        jitter[i].pts_samples = 0;
        jitter[i].frame_samples = 0;
        jitter[i].payload_len = 0;
        jitter[i].flags = 0;
    }
}

/**
 * @brief Counts occupied jitter slots. The caller must hold jitterMutex while inspecting the
 * buffer.
 */
static int countBufferedPacketsNoLock()
{
    return int(jitterBuffered);
}

/**
 * @brief Returns the slot containing seq, or -1 if absent. Requires caller-provided
 * synchronization.
 */
static int findSlotBySeqNoLock(uint32_t seq)
{
    for (int i = 0; i < JITTER_SLOTS; ++i)
    {
        if (jitter[i].used && jitter[i].seq == seq)
            return i;
    }
    return -1;
}

/**
 * @brief Returns the first free slot, or -1 if the buffer is full. Requires caller-provided
 * synchronization.
 */
static int findFreeSlotNoLock()
{
    for (int i = 0; i < JITTER_SLOTS; ++i)
    {
        if (!jitter[i].used)
            return i;
    }
    return -1;
}

/**
 * @brief Stores the lowest buffered sequence in minSeq and reports whether one exists. Requires
 * jitterMutex; leaves UINT32_MAX when empty.
 */
static bool getMinBufferedSeqNoLock(uint32_t &minSeq)
{
    bool found = false;
    minSeq = 0xFFFFFFFFu;

    for (int i = 0; i < JITTER_SLOTS; ++i)
    {
        if (jitter[i].used && jitter[i].seq < minSeq)
        {
            minSeq = jitter[i].seq;
            found = true;
        }
    }
    return found;
}

/**
 * @brief Resets Opus decoder history if a decoder exists, without destroying or recreating it.
 */
static void resetDecoderState()
{
    if (opusDecoder)
        opus_decoder_ctl(opusDecoder, OPUS_RESET_STATE);
}

/**
 * @brief Clears buffered packets and statistics under the mutex, resets Opus and clears playback
 * flags. Requires prior downlink initialization.
 */
static void resetStreamState()
{
    if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
    {
        clearJitterNoLock();

        playbackStarted = false;
        rebuffering = true;
        endSeen = false;
        resumeLogArmed = true;

        expectedSeq = 0;
        highestSeqSeen = 0;
        endSeq = 0;
        endReceivedMs=0;
        consecutiveMisses = 0;
        lastPacketMillis = 0;

        receivedPackets = 0;
        droppedPackets = 0;
        duplicatePackets = 0;
        latePackets = 0;
        aheadDrops = 0;
        missingPackets = 0;
        decodedPackets = 0;
        decodeErrors = 0;
        malformedPackets = 0;
        plcPackets = 0;
        fecRecoveredPackets = 0;
        hardRebuffers = 0;

        xSemaphoreGive(jitterMutex);
    }

    resetDecoderState();

}

/**
 * @brief Copies expectedSeq into out and frees its slot under jitterMutex. Returns false if the
 * packet cannot be obtained.
 */
static bool popExpectedPacketLocked(PacketSlot &out)
{
    bool ok = false;

    if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
    {
        int idx = findSlotBySeqNoLock(expectedSeq);
        if (idx >= 0)
        {
            out = jitter[idx];
            jitter[idx].used = false;
            --jitterBuffered;
            ok = true;
        }
        xSemaphoreGive(jitterMutex);
    }

    return ok;
}

/**
 * @brief Copies the requested packet into out under jitterMutex without consuming it. Returns
 * false if unavailable; used for FEC recovery.
 */
static bool peekPacketBySeqLocked(uint32_t seq, PacketSlot &out)
{
    bool ok = false;

    if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
    {
        int idx = findSlotBySeqNoLock(seq);
        if (idx >= 0)
        {
            out = jitter[idx];
            ok = true;
        }
        xSemaphoreGive(jitterMutex);
    }

    return ok;
}

/**
 * @brief Copies a size-valid Opus packet into a free slot under the mutex. Rejects duplicates
 * and, during playback, late or excessively advanced sequences.
 */
static bool insertPacketLocked(uint32_t seq,
                               uint32_t pts_samples,
                               uint16_t frame_samples,
                               uint16_t payload_len,
                               uint8_t flags,
                               const uint8_t *payload, uint32_t packetEpoch)
{
    bool inserted = false;

    if (payload_len > ServerProtocol::MAX_OPUS_PAYLOAD)
    {
        malformedPackets++;
        droppedPackets++;
        return false;
    }

    if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
    {
        if (!expectResponse.load() || packetEpoch != requestedEpoch.load() || processedEpoch.load() != packetEpoch) {
            xSemaphoreGive(jitterMutex);
            return false;
        }
#if PIUU_NETWORK_DIAGNOSTICS
        downlinkTrace.arrival(seq,millis());
#endif
        if (findSlotBySeqNoLock(seq) >= 0)
        {
            duplicatePackets++;
        }
        else if (playbackStarted && seq < expectedSeq)
        {
            latePackets++;
        }
        else if (seq > expectedSeq + MAX_AHEAD_WINDOW)
        {
            aheadDrops++;
            droppedPackets++;
        }
        else
        {
            int idx = findFreeSlotNoLock();
            if (idx < 0)
            {
                droppedPackets++;
                xSemaphoreGive(jitterMutex);
                return false;
            }

            jitter[idx].used = true;
            ++jitterBuffered;
#if PIUU_PERFORMANCE_DIAGNOSTICS
            if (jitterBuffered>jitterPeak) jitterPeak=jitterBuffered;
#endif
            jitter[idx].seq = seq;
            jitter[idx].pts_samples = pts_samples;
            jitter[idx].frame_samples = frame_samples;
            jitter[idx].payload_len = payload_len;
            jitter[idx].flags = flags;
            memcpy(jitter[idx].payload, payload, payload_len);

            if (receivedPackets == 0 || seq > highestSeqSeen)
                highestSeqSeen = seq;

            if(recoveryEpoch==packetEpoch) recovery.arrived(seq);
            receivedPackets++;
            lastPacketMillis = millis();
            inserted = true;
        }

        xSemaphoreGive(jitterMutex);
    }

    return inserted;
}

/**
 * @brief Records the final stream sequence under the mutex. Playback can finish once pending
 * audio through that boundary has been consumed.
 */
static bool markEndPacket(uint32_t seq, uint32_t packetEpoch)
{
    bool accepted=false;
    if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE) {
        if (expectResponse.load() && packetEpoch==requestedEpoch.load() && processedEpoch.load()==packetEpoch &&
            seq>=expectedSeq && seq-expectedSeq<=JITTER_SLOTS && (!endSeen || seq==endSeq)) {
            if(!endSeen) endReceivedMs=millis();
            endSeen=true;endSeq=seq;accepted=true;
        }
        xSemaphoreGive(jitterMutex);
    }
#if PIUU_PLAYBACK_DIAGNOSTICS
    audioProgress.lastEnd=seq;audioProgress.lastEndEpoch=packetEpoch;
    if(accepted)++audioProgress.endOk;else ++audioProgress.endReject;
#endif
    return accepted;
}

void audioDownlinkPollRecovery() {
    if(!jitterMutex || !expectResponse.load()) return;
    DownlinkRecovery::Request request{};bool send=false;
    if(xSemaphoreTake(jitterMutex,0)==pdTRUE) {
        const uint32_t epoch=requestedEpoch.load();
        if(epoch==processedEpoch.load()) {
            if(recoveryEpoch!=epoch) { recovery.reset();recoveryEpoch=epoch; }
            const uint32_t limit=endSeen?endSeq:(receivedPackets.load()?highestSeqSeen+1:0);
            uint32_t occupied[4]{};
            for(const auto &slot:jitter) if(slot.used && slot.seq>=expectedSeq) {
                const uint32_t offset=slot.seq-expectedSeq;
                if(offset<DownlinkRecovery::WINDOW) occupied[offset/32]|=uint32_t(1)<<(offset%32);
            }
            const uint32_t base=expectedSeq;
            send=recovery.poll(millis(),base,limit,playbackStarted && !rebuffering,
                [&](uint32_t seq) { const uint32_t offset=seq-base;return bool(occupied[offset/32]&(uint32_t(1)<<(offset%32))); },request);
        }
        xSemaphoreGive(jitterMutex);
    }
    if(send) serverLinkRequestDownlink(request.base,request.mask);
}

// ============================================================
// Playback init
// ============================================================
/**
 * @brief Configures DAC I2S output using the audio format and pin assignments. Returns false on
 * an initialization error.
 */
/**
 * @brief Creates the Opus decoder at the configured sample rate and channel count. Returns false
 * if creation fails.
 */
static bool opusInit()
{
    opusDecoder = opus_decoder_create(SAMPLE_RATE_HZ, AUDIO_CHANNELS, &opusErr);
    if (!opusDecoder || opusErr != OPUS_OK)
    {
        appLog().printf("[OPUS] decoder create failed: %d\n", opusErr);
        return false;
    }

    resetDecoderState();
    return true;
}

/**
 * @brief Writes samples PCM16 samples to the DAC. i2s_write may wait indefinitely for driver
 * buffer space.
 */
/**
 * @brief Writes one zero-filled frame to the DAC when usable audio cannot be recovered.
 */
static void writeMedia(const int16_t *pcm,size_t samples) {
#if PIUU_DAC_DIAGNOSTICS
    const uint32_t atMs=millis(),atUs=micros();
#endif
#if PIUU_PLAYBACK_DIAGNOSTICS
    audioProgress.beginWrite(millis(),uint32_t(samples*sizeof(int16_t)));
    const uint32_t outputStartedUs=micros();
#endif
    AudioOutputWriteResult result{};
    {
        Diagnostics::Measure measure(Diagnostics::Operation::OutputWrite);
        result=audioOutputWrite(pcm,samples);
    }
#if PIUU_DAC_DIAGNOSTICS
    dacTrace.record(expectedSeq,atMs,uint32_t(micros()-atUs),result.error,result.bytes,pcm,samples);
#endif
#if PIUU_PLAYBACK_DIAGNOSTICS
    audioProgress.endWrite(uint32_t(micros()-outputStartedUs),result.error,uint32_t(result.bytes));
    AUDIO_PHASE(Advance);
#endif
    lastMediaWriteMs=millis();submittedMediaSamples.store(expectedSeq*320+uint32_t(samples));
    // Partial writes contribute only accepted samples. This estimates DAC progress,
    // not acoustic output; driver/DMA timing must still be measured on hardware.
    const size_t accepted=result.bytes/sizeof(int16_t);
    visualClock.submit(uint32_t(accepted<samples?accepted:samples),lastMediaWriteMs);
    publishVisualClock();
}
static void writeSilenceFrame()
{
    writeMedia(silenceFrame, OPUS_FRAME_SAMPLES);
}

/**
 * @brief Attempts FEC using the next packet, then Opus PLC, then silence. Does not advance
 * expectedSeq; successful decoding does not prove FEC data was present.
 */
static int measuredDecode(OpusDecoder *decoder,const unsigned char *data,opus_int32 bytes,
                          opus_int16 *pcm,int samples,int fec) {
    AUDIO_PHASE(Decode);
    Diagnostics::Measure measure(Diagnostics::Operation::Decode);
    return opus_decode(decoder,data,bytes,pcm,samples,fec);
}
static void decodeAndWriteLossRecovery()
{
    PacketSlot nextPkt;

    if (peekPacketBySeqLocked(expectedSeq + 1, nextPkt))
    {
        int fecSamples = measuredDecode(
            opusDecoder,
            nextPkt.payload,
            nextPkt.payload_len,
            pcmOut,
            OPUS_FRAME_SAMPLES,
            1);

        if (fecSamples > 0)
        {
            writeMedia(pcmOut, fecSamples);
            fecRecoveredPackets++;
            return;
        }
    }

    int plcSamples = measuredDecode(
        opusDecoder,
        nullptr,
        0,
        pcmOut,
        OPUS_FRAME_SAMPLES,
        0);

    if (plcSamples > 0)
    {
        writeMedia(pcmOut, plcSamples);
        plcPackets++;
    }
    else
    {
        writeSilenceFrame();
        decodeErrors++;
    }
}

/**
 * @brief Validates Opus header, version and length, then records END or inserts an audio frame.
 * Returns false if validation or insertion fails.
 */
static bool parseAndQueuePacket(const uint8_t *buf, size_t len, uint32_t packetEpoch)
{
    if (!expectResponse.load() || processedEpoch.load() != requestedEpoch.load()) return true;
    ServerProtocol::OpusPacket packet{};
    if (!ServerProtocol::decodeOpus(buf, len, packet)) return false;
    // Valid transport traffic proves reachability even if jitter rejects its position.
    serverLinkObserveMedia();
    if (packet.end) {
        if(!markEndPacket(packet.sequence, packetEpoch)) return false;
        serverLinkAcknowledgeEnd(packet.sequence);
        appLog().printf("[UDP] END packet seq=%lu\n", (unsigned long)packet.sequence);
        return true;
    }
    return insertPacketLocked(packet.sequence, packet.ptsSamples, packet.frameSamples,
        packet.payloadBytes, packet.flags, packet.payload, packetEpoch);
}

// ============================================================
// Tasks
// ============================================================
/**
 * @brief Polls the socket and passes received datagrams to the Opus parser. Yields when no
 * usable audio data is read.
 * @param parameter Unused FreeRTOS task argument.
 */
#if PIUU_NETWORK_DIAGNOSTICS
static Diagnostics::RxServiceProbe rxService;
#endif

static void udpRxTask(void *parameter)
{
    (void)parameter;
    static uint8_t rxBuf[ServerProtocol::HEADER_SIZE + ServerProtocol::MAX_OPUS_PAYLOAD + 16];

    while (true)
    {
#if PIUU_NETWORK_DIAGNOSTICS
        rxService.begin(micros());
        uint32_t operationStart=micros();
#endif
        const uint32_t packetEpoch=requestedEpoch.load();
        int packetSize = serverLinkParsePacket();
#if PIUU_NETWORK_DIAGNOSTICS
        Diagnostics::RxServiceProbe::maximum(rxService.socketMaxUs,micros()-operationStart);
#endif
        int n = 0;

        if (packetSize > 0)
        {
#if PIUU_NETWORK_DIAGNOSTICS
            operationStart=micros();
#endif
            n = serverLinkRead(rxBuf, sizeof(rxBuf));
#if PIUU_NETWORK_DIAGNOSTICS
            Diagnostics::RxServiceProbe::maximum(rxService.dispatchMaxUs,micros()-operationStart);
#endif
        }

        if (packetSize > 0 && n > 0)
        {
#if PIUU_NETWORK_DIAGNOSTICS
            operationStart=micros();
#endif
            bool ok = parseAndQueuePacket(rxBuf, (size_t)n, packetEpoch);
#if PIUU_NETWORK_DIAGNOSTICS
            Diagnostics::RxServiceProbe::maximum(rxService.queueMaxUs,micros()-operationStart);
#endif
            if (!ok)
                malformedPackets++;
        }
#if PIUU_NETWORK_DIAGNOSTICS
        rxService.end(micros());
#endif
        if (!(packetSize > 0 && n > 0))
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/**
 * @brief Consumes audio at PLAYBACK_PERIOD_MS intervals. Buffers before playback, handles loss
 * or rebuffering, and detects stream completion.
 * @param parameter Unused FreeRTOS task argument.
 */
static void playbackTask(void *parameter)
{
    (void)parameter;
    TickType_t lastWake = xTaskGetTickCount();
    uint32_t epoch = 0;
    bool startedReported = false, finished = false, finishReported = false;

    while (true)
    {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PLAYBACK_PERIOD_MS));

#if PIUU_PLAYBACK_DIAGNOSTICS
        audioProgress.tick(millis());
        audioProgress.flags=uint32_t(startedReported) | (uint32_t(finished)<<1) | (uint32_t(finishReported)<<2);
#endif
        const uint32_t requested = requestedEpoch.load();
        if (epoch != requested) {
            AUDIO_PHASE(Flush);
            audioOutputFlush();
            submittedMediaSamples.store(0);
            visualClock={};publishVisualClock();
            AUDIO_PHASE(Reset);
            resetStreamState();
            epoch = requested;
#if PIUU_DAC_DIAGNOSTICS
            dacTrace.begin(epoch);
#endif
            startedReported = finished = finishReported = false;
            processedEpoch.store(epoch);
        }
        if (!epoch) { AUDIO_PHASE(Idle);continue; }
        if (finished) {
            AUDIO_PHASE(Finished);
            if (!finishReported && eventSink && uint32_t(millis()-lastMediaWriteMs)>=128) finishReported = eventSink({epoch, PlaybackEventKind::Finished});
            continue;
        }
        bool started, rebuf, seenEnd;
        uint32_t expSeq, hiSeq, eSeq, lastPktMs, consecMiss, endAt;
        int buffered = 0;
        uint32_t minSeq = 0;
        bool foundMin = false;
        uint32_t nowMs = millis();
        AUDIO_PHASE(Jitter);

        if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
        {
            started = playbackStarted;
            rebuf = rebuffering;
            seenEnd = endSeen;
            expSeq = expectedSeq;
            hiSeq = highestSeqSeen;
            eSeq = endSeq;endAt=endReceivedMs;
            lastPktMs = lastPacketMillis;
            consecMiss = consecutiveMisses;
            buffered = countBufferedPacketsNoLock();
            foundMin = (buffered > 0) ? getMinBufferedSeqNoLock(minSeq) : false;
            xSemaphoreGive(jitterMutex);
        }
        else
        {
            continue;
        }

        const auto completion=playbackCompletion(seenEnd,expSeq,eSeq,buffered,
            expectResponse.load(),nowMs,lastPktMs?lastPktMs:responseExpectedAt.load(),RESPONSE_STALL_TIMEOUT_MS,
            (rebuf || !started) && !seenEnd && buffered<(started?REBUFFER_PACKETS:START_BUFFERED_PACKETS));
#if PIUU_PERFORMANCE_DIAGNOSTICS
        if (completion!=PlaybackCompletion::Pending && xSemaphoreTake(jitterMutex,portMAX_DELAY)==pdTRUE) {
            pauses.end(nowMs);
            xSemaphoreGive(jitterMutex);
        }
#endif
        if(completion==PlaybackCompletion::Finished) {
            finished=true;expectResponse.store(false);continue;
        }
        if(completion==PlaybackCompletion::Stalled) {
            // Retry the failure event if its bounded queue is full. Never emit PLAYED.
            if(eventSink && eventSink({epoch,PlaybackEventKind::Failed})) {
                appLog().printf("[PLAYBACK] stalled epoch=%lu seq=%lu end=%d idle_ms=%lu\n",
                    (unsigned long)epoch,(unsigned long)expSeq,seenEnd,
                    (unsigned long)(nowMs-(lastPktMs?lastPktMs:responseExpectedAt.load())));
                finished=finishReported=true;expectResponse.store(false);
            }
            continue;
        }

        if (rebuf || !started)
        {
            bool canResume = false;
            int needed = started ? REBUFFER_PACKETS : START_BUFFERED_PACKETS;

            if (seenEnd) {
                // END proves the boundary, not the arrival of every last frame.
                // Give trailing recovery a bounded grace; then conceal through END.
                canResume = playbackCanDrainEnd(expSeq,eSeq,foundMin,minSeq,nowMs,endAt,DownlinkRecovery::END_GRACE_MS);
            }
            else
                canResume = (buffered >= needed && foundMin);

            if (canResume)
            {
                if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
                {
#if PIUU_NETWORK_DIAGNOSTICS
                    downlinkTrace.resume(expectedSeq,minSeq,nowMs,!playbackStarted);
#endif
                    if(!seenEnd) expectedSeq = minSeq;
                    playbackStarted = true;
                    rebuffering = false;
#if PIUU_PERFORMANCE_DIAGNOSTICS || PIUU_NETWORK_DIAGNOSTICS
                    pauses.end(nowMs);
#endif
                    consecutiveMisses = 0;
                    bool shouldLog = resumeLogArmed;
                    resumeLogArmed = false;
                    xSemaphoreGive(jitterMutex);

                    if (shouldLog)
                    {
                        appLog().printf("[PLAYBACK] start/resume at seq=%lu buffered=%d\n",
                                      (unsigned long)minSeq, buffered);
                    }
                    if (!startedReported && eventSink) startedReported = eventSink({epoch, PlaybackEventKind::Started});
                }
            }
            else
            {
                continue;
            }
        }

        AUDIO_PHASE(Pop);
        if (!popExpectedPacketLocked(playbackPkt))
        {
            const bool drainingEnd=seenEnd && expSeq<eSeq;
            bool bufferIsActuallyLow = !drainingEnd && (buffered <= LOW_WATER_PACKETS);
            bool tooManyMissesWithLowBuffer = !drainingEnd && ((consecMiss + 1) >= MAX_CONSECUTIVE_MISSES) &&
                                              (buffered <= (REBUFFER_PACKETS / 2));
            bool seqGapTooLarge = (hiSeq > expSeq) && ((hiSeq - expSeq) > HARD_REBUFFER_GAP);
            bool shouldHardRebuffer = !drainingEnd && seqGapTooLarge && ((consecMiss + 1) >= HARD_REBUFFER_MISSES);

            if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
            {
                missingPackets++;
                consecutiveMisses++;

                if (bufferIsActuallyLow || tooManyMissesWithLowBuffer || shouldHardRebuffer)
                {
#if PIUU_PERFORMANCE_DIAGNOSTICS || PIUU_NETWORK_DIAGNOSTICS
                    if (playbackStarted && !rebuffering) pauses.begin(nowMs);
#endif
                    rebuffering = true;
                    resumeLogArmed = true;
                    if (shouldHardRebuffer)
                        hardRebuffers++;
                }

                xSemaphoreGive(jitterMutex);
            }

            if (bufferIsActuallyLow || tooManyMissesWithLowBuffer || shouldHardRebuffer)
                continue;

            decodeAndWriteLossRecovery();
            AUDIO_PHASE(Advance);
            if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
            {
                expectedSeq++;
#if PIUU_PLAYBACK_DIAGNOSTICS
                audioProgress.advance(millis());
#endif
                xSemaphoreGive(jitterMutex);
            }
            continue;
        }
        if (!startedReported && eventSink) startedReported = eventSink({epoch, PlaybackEventKind::Started});

        if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
        {
            consecutiveMisses = 0;
            xSemaphoreGive(jitterMutex);
        }

        int decodedSamples = measuredDecode(
            opusDecoder,
            playbackPkt.payload,
            playbackPkt.payload_len,
            pcmOut,
            OPUS_FRAME_SAMPLES,
            0);

        if (decodedSamples < 0)
        {
            decodeErrors++;
            decodeAndWriteLossRecovery();
        }
        else
        {
            writeMedia(pcmOut, decodedSamples);
            decodedPackets++;
        }

        AUDIO_PHASE(Advance);
        if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
        {
            expectedSeq++;
#if PIUU_PLAYBACK_DIAGNOSTICS
            audioProgress.advance(millis());
#endif
            xSemaphoreGive(jitterMutex);
        }
    }
}

/**
 * @brief Periodically logs reception, discard, recovery and playback counters for audio
 * diagnostics.
 * @param parameter Unused FreeRTOS task argument.
 */

#if PIUU_PLAYBACK_DIAGNOSTICS
static void pollAudioProgress() {
    static uint32_t reportAt=0,lineAt=0;
    static unsigned line=4;
    struct Snapshot {
        uint32_t at=0,epoch=0,seq=0,high=0,queued=0,final=0,rxAge=0;
        bool locked=false,end=false,rebuf=false;
    };
    static Snapshot snapshot;
    const uint32_t now=millis();
    if(uint32_t(now-reportAt)>=5000) {
        reportAt=now;line=0;snapshot=Snapshot{};snapshot.at=now;
        if(jitterMutex && xSemaphoreTake(jitterMutex,0)==pdTRUE) {
            snapshot.locked=true;snapshot.epoch=processedEpoch.load();snapshot.seq=expectedSeq;snapshot.high=highestSeqSeen;
            snapshot.queued=jitterBuffered;snapshot.final=endSeq;
            snapshot.end=endSeen;snapshot.rebuf=rebuffering;
            snapshot.rxAge=lastPacketMillis?Diagnostics::AudioProgress::age(now,lastPacketMillis):0;
            xSemaphoreGive(jitterMutex);
        }
    }
    if(line>=4 || uint32_t(now-lineAt)<100)return;
    lineAt=now;
    switch(line++) {
    case 0:
        appLog().printf("[AUDIO_RX t=%lu] req=%lu proc=%lu waiting=%d rx=%lu dec=%lu dec_err=%lu drop=%lu plc=%lu rx_age_ms=%lu\n",
            (unsigned long)now,(unsigned long)requestedEpoch.load(),(unsigned long)processedEpoch.load(),
            expectResponse.load(),(unsigned long)receivedPackets.load(),(unsigned long)decodedPackets.load(),
            (unsigned long)decodeErrors.load(),(unsigned long)droppedPackets.load(),
            (unsigned long)plcPackets.load(),(unsigned long)snapshot.rxAge);
        break;
    case 1:
        appLog().printf("[AUDIO_BUFFER t=%lu] locked=%d snap_t=%lu epoch=%lu seq=%lu high=%lu queued=%lu end=%d final=%lu rebuf=%d\n",
            (unsigned long)now,snapshot.locked,(unsigned long)snapshot.at,(unsigned long)snapshot.epoch,(unsigned long)snapshot.seq,
            (unsigned long)snapshot.high,(unsigned long)snapshot.queued,snapshot.end,
            (unsigned long)snapshot.final,snapshot.rebuf);
        break;
    case 2:
        appLog().printf("[AUDIO_WORKER t=%lu] phase=%s phase_age_ms=%lu loops=%lu loop_age_ms=%lu progress_age_ms=%lu flags=%lu played_ms=%lu\n",
            (unsigned long)now,Diagnostics::AudioProgress::name(audioProgress.phase.load()),
            (unsigned long)Diagnostics::AudioProgress::age(now,audioProgress.phaseAt.load()),(unsigned long)audioProgress.loops.load(),
            (unsigned long)Diagnostics::AudioProgress::age(now,audioProgress.loopAt.load()),
            (unsigned long)Diagnostics::AudioProgress::age(now,audioProgress.advancedAt.load()),
            (unsigned long)audioProgress.flags.load(),(unsigned long)audioDownlinkPlayedMs());
        break;
    case 3:
        appLog().printf("[AUDIO_WRITE t=%lu] calls=%lu last_us=%lu max_us=%lu err=%ld bytes=%lu wanted=%lu end_ok=%lu end_reject=%lu last_end=%lu end_epoch=%lu\n",
            (unsigned long)now,(unsigned long)audioProgress.writes.load(),(unsigned long)audioProgress.writeUs.load(),
            (unsigned long)audioProgress.writeMaxUs.load(),(long)audioProgress.error.load(),
            (unsigned long)audioProgress.bytes.load(),(unsigned long)audioProgress.wanted.load(),
            (unsigned long)audioProgress.endOk.load(),(unsigned long)audioProgress.endReject.load(),
            (unsigned long)audioProgress.lastEnd.load(),(unsigned long)audioProgress.lastEndEpoch.load());
        break;
    }
}
#endif

void audioDownlinkPollDiagnostics() {
#if PIUU_PLAYBACK_DIAGNOSTICS
    pollAudioProgress();
#endif
#if PIUU_DAC_DIAGNOSTICS
    Diagnostics::DacTrace::Sample dacSample{};
    // One line per loop pass; playback never prints diagnostic dacSamples.
    if(dacTrace.pop(dacSample)) {
        appLog().printf("[DAC_WRITE t=%lu] epoch=%lu seq=%lu write_us=%lu err=%ld bytes=%lu expected=%lu peak=%lu mean_abs=%lu errors=%lu incomplete=%lu trace_drop=%lu\n",
            (unsigned long)dacSample.atMs,(unsigned long)dacSample.epoch,(unsigned long)dacSample.sequence,
            (unsigned long)dacSample.writeUs,(long)dacSample.error,(unsigned long)dacSample.bytes,
            (unsigned long)dacSample.expectedBytes,(unsigned long)dacSample.peak,(unsigned long)dacSample.meanAbs,
            (unsigned long)dacTrace.errorCount(),(unsigned long)dacTrace.incompleteCount(),
            (unsigned long)dacTrace.droppedCount());
    }
#endif
#if PIUU_NETWORK_DIAGNOSTICS
    static uint32_t lastReport=0,lastLine=0;
    static unsigned remaining=0;
    const uint32_t now=millis();
    if(uint32_t(now-lastReport)>=5000) {
        if(!jitterMutex || xSemaphoreTake(jitterMutex,0)!=pdTRUE)return;
        const uint32_t epoch=processedEpoch.load();
        const uint32_t arrivals=downlinkTrace.arrivals,next=downlinkTrace.nextSequence;
        const uint32_t gaps=downlinkTrace.forwardEvents,forward=downlinkTrace.forwardFrames;
        const uint32_t reordered=downlinkTrace.reordered,maxGap=downlinkTrace.maxInterval;
        const uint32_t longGaps=downlinkTrace.longIntervals,skipped=downlinkTrace.skipped;
        const uint32_t omitted=downlinkTrace.sampleDropped;
        // Read time after locking: RX may have advanced lastTime since poll began.
        const uint32_t capturedAt=millis();
        const uint32_t age=downlinkTrace.seen?uint32_t(capturedAt-downlinkTrace.lastTime):0;
        const auto pause=pauses;
        xSemaphoreGive(jitterMutex);
        lastReport=lastLine=now;remaining=8;
        appLog().printf("[UDP_RX_SERVICE t=%lu] idle_max_us=%lu work_max_us=%lu socket_max_us=%lu dispatch_max_us=%lu queue_max_us=%lu\n",
            (unsigned long)now,(unsigned long)rxService.idleMaxUs.load(),
            (unsigned long)rxService.workMaxUs.load(),(unsigned long)rxService.socketMaxUs.load(),
            (unsigned long)rxService.dispatchMaxUs.load(),(unsigned long)rxService.queueMaxUs.load());
        appLog().printf("[OPUS_RX t=%lu] epoch=%lu arrivals=%lu next=%lu gap_events=%lu forward_frames=%lu reorder_or_dup=%lu max_interval_ms=%lu intervals_ge100ms=%lu rx_age_ms=%lu skipped_frames=%lu sample_drop=%lu rebuf_n=%lu rebuf_total_ms=%lu rebuf_max_ms=%lu rebuf_active_ms=%lu\n",
            (unsigned long)now,(unsigned long)epoch,(unsigned long)arrivals,(unsigned long)next,
            (unsigned long)gaps,(unsigned long)forward,(unsigned long)reordered,(unsigned long)maxGap,
            (unsigned long)longGaps,(unsigned long)age,(unsigned long)skipped,(unsigned long)omitted,
            (unsigned long)pause.count,(unsigned long)pause.totalMs,(unsigned long)pause.maxMs,
            (unsigned long)pause.activeMs(capturedAt));
        return;
    }
    if(!remaining || uint32_t(now-lastLine)<80)return;
    if(!jitterMutex || xSemaphoreTake(jitterMutex,0)!=pdTRUE)return;
    Diagnostics::DownlinkSample sample;
    const bool found=downlinkTrace.take(sample);
    const uint32_t epoch=processedEpoch.load();
    xSemaphoreGive(jitterMutex);
    if(!found) { remaining=0;return; }
    --remaining;lastLine=now;
    appLog().printf("[OPUS_GAP t=%lu] epoch=%lu stage=%s expected=%lu received=%lu interval_ms=%lu skipped_frames=%lu\n",
        (unsigned long)sample.time,(unsigned long)epoch,
        sample.resume?(sample.initial?"initial":"resume"):"arrival",
        (unsigned long)sample.expected,(unsigned long)sample.received,
        (unsigned long)sample.interval,(unsigned long)sample.skipped);
#endif
}

// Called by loop every five seconds; never print while holding the jitter lock.
void audioDownlinkLogDiagnostics() {
#if PIUU_DIAGNOSTICS
    if(!jitterMutex || xSemaphoreTake(jitterMutex,0)!=pdTRUE) return;
    const int buffered=countBufferedPacketsNoLock();
    const uint32_t seq=expectedSeq;
    const bool ended=endSeen,rebuf=rebuffering;
    const uint32_t nackRequests=recovery.requests,nackFrames=recovery.frames,recovered=recovery.recovered;
    xSemaphoreGive(jitterMutex);
    appLog().printf("[DOWNLINK_RECOVERY t=%lu] epoch=%lu nack_requests=%lu nack_frames=%lu recovered=%lu\n",
        (unsigned long)millis(),(unsigned long)recoveryEpoch,(unsigned long)nackRequests,
        (unsigned long)nackFrames,(unsigned long)recovered);
    appLog().printf("[AUDIO t=%lu] epoch=%lu rx=%lu dec=%lu miss=%lu fec=%lu plc=%lu drop=%lu dup=%lu late=%lu ahead=%lu bad=%lu dec_err=%lu hard_rebuf=%lu buffered=%d seq=%lu end=%d rebuf=%d played_est_ms=%lu\n",
        (unsigned long)millis(),(unsigned long)processedEpoch.load(),
        (unsigned long)receivedPackets.load(),(unsigned long)decodedPackets.load(),
        (unsigned long)missingPackets.load(),(unsigned long)fecRecoveredPackets.load(),
        (unsigned long)plcPackets.load(),(unsigned long)droppedPackets.load(),
        (unsigned long)duplicatePackets.load(),(unsigned long)latePackets.load(),
        (unsigned long)aheadDrops.load(),(unsigned long)malformedPackets.load(),
        (unsigned long)decodeErrors.load(),(unsigned long)hardRebuffers.load(),
        buffered,(unsigned long)seq,ended,rebuf,(unsigned long)audioDownlinkPlayedMs());
#endif
}

// ============================================================
// Public API
// ============================================================
/**
 * @brief Creates the jitter mutex and initializes I2S and Opus. Returns false on initialization
 * failure and resets stream state on success.
 */
static bool downlinkInitialized = false;
static std::atomic_bool tasksReady{false};
struct DownlinkTask {
    TaskFunction_t entry;
    const char *name;
    uint32_t stack;
    UBaseType_t priority;
    TaskHandle_t handle;
};
static DownlinkTask downlinkTasks[] = {
    {udpRxTask, "udpRxTask", 3072, 3, nullptr},
    {playbackTask, "playbackTask", 8192, 2, nullptr},

};

bool audioDownlinkInit()
{
    if (downlinkInitialized) return true;
    jitterMutex = xSemaphoreCreateMutex();
    if (!jitterMutex) return false;
    if (!audioOutputInit()) {
        vSemaphoreDelete(jitterMutex);
        jitterMutex = nullptr;
        return false;
    }
    if (!opusInit()) {
        audioOutputDeinit();
        vSemaphoreDelete(jitterMutex);
        jitterMutex = nullptr;
        return false;
    }
    resetStreamState();
    downlinkInitialized = true;
    return true;
}

// No worker may access UDP/jitter/decoder until all tasks exist. This permits
// deleting a partial startup without abandoning a mutex held by a worker.
static void downlinkTaskEntry(void *parameter)
{
    auto *task = static_cast<DownlinkTask *>(parameter);
    while (!tasksReady.load()) vTaskDelay(pdMS_TO_TICKS(1));
    task->entry(nullptr);
    vTaskDelete(nullptr);
}

bool audioDownlinkStartTasks()
{
    if (tasksReady.load()) return true;
    if (!downlinkInitialized) return false;
    for (auto &task : downlinkTasks) {
        if (xTaskCreatePinnedToCore(downlinkTaskEntry, task.name, task.stack,
                &task, task.priority, &task.handle, 1) != pdPASS) {
            for (auto &created : downlinkTasks) {
                if (created.handle) vTaskDelete(created.handle);
                created.handle = nullptr;
            }
            appLog().println("[DOWNLINK] Task startup failed; partial workers removed");
            return false;
        }
    }
    tasksReady.store(true);
    return true;
}

void audioDownlinkLogStacks() {
#if PIUU_DIAGNOSTICS
    for(const auto &task:downlinkTasks) if(task.handle)
        appLog().printf("[STACK t=%lu] task=%s hwm_bytes=%lu\n",(unsigned long)millis(),task.name,
            (unsigned long)uxTaskGetStackHighWaterMark(task.handle));
#endif
}

bool audioDownlinkBufferStats(AudioBufferStats &stats) {
#if PIUU_PERFORMANCE_DIAGNOSTICS
    if (!jitterMutex || xSemaphoreTake(jitterMutex,0)!=pdTRUE) return false;
    stats={jitterBuffered,jitterPeak,pauses.count,pauses.totalMs,pauses.maxMs,pauses.activeMs(millis())};
    xSemaphoreGive(jitterMutex);return true;
#else
    (void)stats;return false;
#endif
}
