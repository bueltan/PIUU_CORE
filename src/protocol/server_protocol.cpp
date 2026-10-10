#include "protocol/server_protocol.h"
#include <cstring>
#include <initializer_list>

namespace ServerProtocol {
namespace {
uint16_t read16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
uint32_t read32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void write16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
void write32(uint8_t *p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
}
Bytes controlMessage(Control control) {
    static const uint8_t ping[] = {'P','I','N','G'};
    static const uint8_t pong[] = {'P','O','N','G'};
    static const uint8_t commit[] = {'C','O','M','M','I','T'};
    switch (control) {
        case Control::Ping: return {ping, sizeof(ping)};
        case Control::Pong: return {pong, sizeof(pong)};
        case Control::Commit: return {commit, sizeof(commit)};
        default: return {nullptr, 0};
    }
}
Control parseControl(const uint8_t *data, size_t size) {
    if (!data) return Control::Unknown;
    for (Control c : {Control::Ping, Control::Pong, Control::Commit}) {
        const Bytes b = controlMessage(c);
        if (size == b.size && !memcmp(data, b.data, size)) return c;
    }
    return Control::Unknown;
}
size_t encodePcm(uint8_t *out, size_t capacity, uint32_t sequence,
                 uint32_t ptsSamples, const int16_t *pcm, size_t samples, bool end) {
    if (end ? (samples != 0 || pcm != nullptr) : (!pcm || samples != PCM_FRAME_SAMPLES)) return 0;
    const size_t payloadBytes = end ? 0 : PCM_FRAME_BYTES;
    if (!out || capacity < HEADER_SIZE + payloadBytes) return 0;
    memcpy(out, "PCM!", 4); out[4] = VERSION; out[5] = end ? FLAG_END : 0;
    write32(out + 6, sequence); write32(out + 10, ptsSamples);
    write16(out + 14, uint16_t(samples)); write16(out + 16, uint16_t(payloadBytes));
    // ESP32's native PCM16 is little-endian, as required by the bridge.
    if (payloadBytes) memcpy(out + HEADER_SIZE, pcm, payloadBytes);
    return HEADER_SIZE + payloadBytes;
}
bool decodeOpus(const uint8_t *data, size_t size, OpusPacket &out) {
    if (!data || size < HEADER_SIZE || memcmp(data, "OPUS", 4) || data[4] != VERSION) return false;
    const uint16_t samples = read16(data + 14), bytes = read16(data + 16);
    const bool end = (data[5] & FLAG_END) != 0;
    if (size != HEADER_SIZE + bytes || bytes > MAX_OPUS_PAYLOAD) return false;
    if (end ? (samples != 0 || bytes != 0) : (samples != OPUS_FRAME_SAMPLES || bytes == 0)) return false;
    out = {read32(data + 6), read32(data + 10), samples, bytes, data[5], data + HEADER_SIZE, end};
    return true;
}
}
