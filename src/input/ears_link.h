#pragma once
#include <stdint.h>
#include "protocol/ears_protocol.h"
struct EarsAudioFrame { uint32_t order,sequence; int16_t samples[PCM_FRAME_SAMPLES]; };
struct EarsEvent { uint32_t order; uint8_t code,size; uint8_t payload[EarsProtocol::MAX_EVENT_BYTES]; };
struct EarsMessage {
    EarsProtocol::Kind kind;
    union { EarsAudioFrame audio; EarsEvent event; };
};
struct EarsLinkStats { uint32_t audioReceived,audioDropped,eventDropped,parseRejected,commandsSent; bool remoteStreaming; };
// setup/loop own lifecycle, command intent and queue consumption. Only RX/TX
// worker touches HardwareSerial. Workers never touch conversation/UI/settings.
bool earsLinkBegin();
bool earsLinkRequestStreaming(bool enabled);
bool earsLinkTake(EarsMessage &message);
uint32_t earsLinkReceivedOrder();
bool earsLinkPeerOnline();
EarsLinkStats earsLinkStats();
struct EarsQueueStats { uint32_t audio, audioPeak, events, eventPeak; };
EarsQueueStats earsLinkQueueStats();
