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
// Register before earsLinkBegin. Called on UART worker; copy only, no blocking.
using EarsEyesSink=void(*)(const uint8_t*,size_t);
void earsLinkSetEyesSink(EarsEyesSink);
bool earsLinkRequestStreaming(bool enabled);
bool earsLinkTake(EarsMessage &message);
uint32_t earsLinkReceivedOrder();
bool earsLinkPeerOnline();
EarsLinkStats earsLinkStats();
struct EarsQueueStats { uint32_t audio, audioPeak, events, eventPeak; };
EarsQueueStats earsLinkQueueStats();
struct EarsRxStats { uint32_t bytes,blocks,maxBlock; };
EarsRxStats earsLinkRxStats();

#include "diagnostics/config.h"
struct EarsUartStats { uint32_t fifoOverflow,bufferFull,frameError,parityError,breakError,
    rxGaps,rxMissing,sequenceResets,samplesDropped; };
struct EarsGapSample { uint32_t timeMs,expected,received,order; bool consumer; };
#if PIUU_UART_DIAGNOSTICS
EarsUartStats earsLinkUartStats();
bool earsLinkTakeGapSample(EarsGapSample &sample);
void earsLinkRecordConsumerGap(uint32_t expected,uint32_t received,uint32_t order);
#else
inline EarsUartStats earsLinkUartStats() { return {}; }
inline bool earsLinkTakeGapSample(EarsGapSample &) { return false; }
inline void earsLinkRecordConsumerGap(uint32_t,uint32_t,uint32_t) {}
#endif
