#pragma once
#include <stddef.h>
#include <stdint.h>
#include "config/audio_format.h"

// Python bridge v1. Headers are big-endian, PCM16 payload little-endian.
// No transport, Arduino, allocation, application state or UART framing here.
namespace ServerProtocol {
constexpr uint8_t VERSION = 1;
constexpr uint8_t FLAG_END = 0x01;
constexpr size_t HEADER_SIZE = 18;
constexpr size_t PCM_PACKET_MAX = HEADER_SIZE + PCM_FRAME_BYTES;
constexpr size_t MAX_OPUS_PAYLOAD = 300;

enum class Control { Unknown, Ping, Pong, Commit };
struct Bytes { const uint8_t *data; size_t size; };
Bytes controlMessage(Control control);
Control parseControl(const uint8_t *data, size_t size);

// Returns bytes written, or 0 for invalid arguments. Writes into caller memory.
size_t encodePcm(uint8_t *out, size_t capacity, uint32_t sequence,
                 uint32_t ptsSamples, const int16_t *pcm, size_t samples, bool end);
struct OpusPacket {
    uint32_t sequence;
    uint32_t ptsSamples;
    uint16_t frameSamples;
    uint16_t payloadBytes;
    uint8_t flags;
    const uint8_t *payload; // Borrowed view: valid only while datagram lives.
    bool end;
};
bool decodeOpus(const uint8_t *data, size_t size, OpusPacket &out);
}
