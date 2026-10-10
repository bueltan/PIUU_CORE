#include "hardware/app_log.h"
#include "audio/audio_uplink.h"
#include "config/app_config.h"
#include "protocol/server_protocol.h"

static AudioUplinkSender g_rawUdpSender = nullptr;
static uint32_t uplinkSequence = 0;
static uint32_t uplinkPtsSamples = 0;
static uint8_t pcmUplinkPacket[ServerProtocol::PCM_PACKET_MAX];
static bool active = false;
static bool closing = false;
static uint8_t endsRemaining = 0;
static uint32_t lastCloseActionMs = 0;

static bool sendPcmAudioPacket(uint32_t seq, uint32_t ptsSamples,
                               const int16_t *pcm, size_t samples, bool end)
{
    const size_t size = ServerProtocol::encodePcm(pcmUplinkPacket, sizeof(pcmUplinkPacket),
        seq, ptsSamples, pcm, samples, end);
    return size && g_rawUdpSender && g_rawUdpSender(pcmUplinkPacket, size);
}

/**
 * @brief Sends an empty PCM END marker carrying the turn's final sequence and sample position.
 */
static void sendSinglePcmEndPacket()
{
    const bool ok = sendPcmAudioPacket(uplinkSequence, uplinkPtsSamples, nullptr, 0, true);

    appLog().printf("[UPLINK] sent PCM END seq=%lu ok=%d\n",
                  (unsigned long)uplinkSequence,
                  ok ? 1 : 0);
}

/**
 * @brief Sends COMMIT to ask the server to finalize input and generate a response. Only logs the
 * local transmission result.
 */
static void sendCommitPacket()
{
    const auto message = ServerProtocol::controlMessage(ServerProtocol::Control::Commit);
    const bool ok = g_rawUdpSender && g_rawUdpSender(message.data, message.size);
    appLog().printf("[UPLINK] sent COMMIT ok=%d\n", ok ? 1 : 0);
}


void audioUplinkSetSender(AudioUplinkSender sender) { g_rawUdpSender = sender; }

bool audioUplinkBeginTurn()
{
    if (active || closing || !g_rawUdpSender) return false;
    uplinkSequence = 0;
    uplinkPtsSamples = 0;
    active = true;
    return true;
}

bool audioUplinkSubmitFrame(const int16_t *pcm, size_t samples)
{
    if (!active || !pcm || samples != PCM_FRAME_SAMPLES) return false;
    const bool ok = sendPcmAudioPacket(uplinkSequence, uplinkPtsSamples,
        pcm, samples, false);
    ++uplinkSequence;
    uplinkPtsSamples += PCM_FRAME_SAMPLES;
    return ok;
}

void audioUplinkEndTurn()
{
    if (!active) return;
    active = false;
    closing = true;
    endsRemaining = PCM_END_REPEAT_COUNT;
    lastCloseActionMs = millis() - PCM_END_REPEAT_GAP_MS;
}

void audioUplinkPoll()
{
    if (!closing) return;
    const uint32_t now = millis();
    if (endsRemaining) {
        if (uint32_t(now - lastCloseActionMs) < PCM_END_REPEAT_GAP_MS) return;
        sendSinglePcmEndPacket();
        --endsRemaining;
        lastCloseActionMs = now;
    } else if (uint32_t(now - lastCloseActionMs) >= COMMIT_DELAY_MS) {
        sendCommitPacket();
        closing = false;
    }
}

bool audioUplinkIsClosing() { return closing; }

void audioUplinkAbortTurn() { active = closing = false; endsRemaining = 0; }

void audioUplinkSkipFrames(uint32_t count) {
    if (!active) return;
    uplinkSequence+=count;
    uplinkPtsSamples+=count*PCM_FRAME_SAMPLES;
}
