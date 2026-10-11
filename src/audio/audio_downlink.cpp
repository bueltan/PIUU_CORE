#include "hardware/app_log.h"
/**
 * @file audio_downlink.cpp
 * @brief UDP Opus reception, sequence-based buffering and I2S playback. The jitter buffer
 * absorbs timing variation; FEC and PLC attempt loss recovery.
 */

#include "audio/audio_downlink.h"
#include "diagnostics/performance.h"
#include "diagnostics/metrics.h"
#include "audio/playback_completion.h"

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
static Diagnostics::Rebuffer pauses;
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
    jitterPeak=0; pauses=Diagnostics::Rebuffer{};
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
        if (findSlotBySeqNoLock(seq) >= 0)
        {
            duplicatePackets++;
        }
        else if (playbackStarted && !rebuffering && seq < expectedSeq)
        {
            latePackets++;
        }
        else if (playbackStarted && !rebuffering && seq > expectedSeq + MAX_AHEAD_WINDOW)
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
static void markEndPacket(uint32_t seq, uint32_t packetEpoch)
{
    if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
    {
        if (expectResponse.load() && packetEpoch == requestedEpoch.load() && processedEpoch.load() == packetEpoch) {
            endSeen = true;
            endSeq = seq;
        }
        xSemaphoreGive(jitterMutex);
    }
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
    {
        Diagnostics::Measure measure(Diagnostics::Operation::OutputWrite);
        audioOutputWrite(pcm,samples);
    }
    lastMediaWriteMs=millis();submittedMediaSamples.store(expectedSeq*320+uint32_t(samples));
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
        markEndPacket(packet.sequence, packetEpoch);
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
static void udpRxTask(void *parameter)
{
    (void)parameter;
    static uint8_t rxBuf[ServerProtocol::HEADER_SIZE + ServerProtocol::MAX_OPUS_PAYLOAD + 16];

    while (true)
    {
        const uint32_t packetEpoch=requestedEpoch.load();
        int packetSize = serverLinkParsePacket();
        int n = 0;

        if (packetSize > 0)
        {
            n = serverLinkRead(rxBuf, sizeof(rxBuf));
        }

        if (packetSize > 0 && n > 0)
        {
            bool ok = parseAndQueuePacket(rxBuf, (size_t)n, packetEpoch);
            if (!ok)
                malformedPackets++;
        }
        else
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

        const uint32_t requested = requestedEpoch.load();
        if (epoch != requested) {
            audioOutputFlush();
            submittedMediaSamples.store(0);
            resetStreamState();
            epoch = requested;
            startedReported = finished = finishReported = false;
            processedEpoch.store(epoch);
        }
        if (!epoch) continue;
        if (finished) {
            if (!finishReported && eventSink && uint32_t(millis()-lastMediaWriteMs)>=128) finishReported = eventSink({epoch, PlaybackEventKind::Finished});
            continue;
        }
        bool started, rebuf, seenEnd;
        uint32_t expSeq, hiSeq, eSeq, lastPktMs, consecMiss;
        int buffered = 0;
        uint32_t minSeq = 0;
        bool foundMin = false;
        uint32_t nowMs = millis();

        if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
        {
            started = playbackStarted;
            rebuf = rebuffering;
            seenEnd = endSeen;
            expSeq = expectedSeq;
            hiSeq = highestSeqSeen;
            eSeq = endSeq;
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

            if (seenEnd)
                canResume = (buffered > 0 && foundMin);
            else
                canResume = (buffered >= needed && foundMin);

            if (canResume)
            {
                if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
                {
                    expectedSeq = minSeq;
                    playbackStarted = true;
                    rebuffering = false;
#if PIUU_PERFORMANCE_DIAGNOSTICS
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

        if (!popExpectedPacketLocked(playbackPkt))
        {
            bool bufferIsActuallyLow = (buffered <= LOW_WATER_PACKETS);
            bool tooManyMissesWithLowBuffer = ((consecMiss + 1) >= MAX_CONSECUTIVE_MISSES) &&
                                              (buffered <= (REBUFFER_PACKETS / 2));
            bool seqGapTooLarge = (hiSeq > expSeq) && ((hiSeq - expSeq) > HARD_REBUFFER_GAP);
            bool shouldHardRebuffer = seqGapTooLarge && ((consecMiss + 1) >= HARD_REBUFFER_MISSES);

            if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
            {
                missingPackets++;
                consecutiveMisses++;

                if (bufferIsActuallyLow || tooManyMissesWithLowBuffer || shouldHardRebuffer)
                {
#if PIUU_PERFORMANCE_DIAGNOSTICS
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

            if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
            {
                expectedSeq++;
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

        if (xSemaphoreTake(jitterMutex, portMAX_DELAY) == pdTRUE)
        {
            expectedSeq++;
            xSemaphoreGive(jitterMutex);
        }
    }
}

/**
 * @brief Periodically logs reception, discard, recovery and playback counters for audio
 * diagnostics.
 * @param parameter Unused FreeRTOS task argument.
 */

// Called by loop every five seconds; never print while holding the jitter lock.
void audioDownlinkLogDiagnostics() {
#if PIUU_DIAGNOSTICS
    if(!jitterMutex || xSemaphoreTake(jitterMutex,0)!=pdTRUE) return;
    const int buffered=countBufferedPacketsNoLock();
    const uint32_t seq=expectedSeq;
    const bool ended=endSeen,rebuf=rebuffering;
    xSemaphoreGive(jitterMutex);
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
