#include "hardware/app_log.h"
#include "audio/audio_uplink.h"
#include "config/app_config.h"
#include "protocol/server_protocol.h"

static AudioUplinkSender g_rawUdpSender = nullptr;
static uint32_t uplinkSequence = 0;
static uint32_t uplinkPtsSamples = 0;
static uint8_t pcmUplinkPacket[ServerProtocol::PCM_PACKET_MAX];
static bool active = false;
static bool sendPcmAudioPacket(uint32_t seq, uint32_t ptsSamples,
                               const int16_t *pcm, size_t samples)
{
    const size_t size=ServerProtocol::encodePcm(pcmUplinkPacket,sizeof(pcmUplinkPacket),
        seq,ptsSamples,pcm,samples,false);
    return size && g_rawUdpSender && g_rawUdpSender(pcmUplinkPacket,size);
}

void audioUplinkSetSender(AudioUplinkSender sender) { g_rawUdpSender = sender; }

bool audioUplinkBeginConversation()
{
    if (active || !g_rawUdpSender) return false;
    uplinkSequence = 0;
    uplinkPtsSamples = 0;
    active = true;
    return true;
}

bool audioUplinkSubmitFrame(const int16_t *pcm, size_t samples)
{
    if (!active || !pcm || samples != PCM_FRAME_SAMPLES) return false;
    const bool ok = sendPcmAudioPacket(uplinkSequence, uplinkPtsSamples,
        pcm, samples);
    ++uplinkSequence;
    uplinkPtsSamples += PCM_FRAME_SAMPLES;
    return ok;
}

void audioUplinkClose() { active=false; }

void audioUplinkSkipFrames(uint32_t count) {
    if (!active) return;
    uplinkSequence+=count;
    uplinkPtsSamples+=count*PCM_FRAME_SAMPLES;
}
