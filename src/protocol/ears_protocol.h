#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config/audio_format.h"

namespace EarsProtocol {
constexpr uint8_t VERSION=1;
constexpr size_t PCM_HEADER_BYTES=10, CONTROL_HEADER_BYTES=8, MAX_EVENT_BYTES=32;
enum class Event : uint8_t { Wake=0x01, Lights=0x02, Stop=0x03, Mute=0x04, Eyes=0x05, StreamState=0x80 };
enum class Command : uint8_t { Start=0x01, Stop=0x02, StartWithKeywords=0x03, DiagnosticLog=0x10 };
enum class Kind : uint8_t { Pcm, Event };
struct PacketView {
    Kind kind;
    uint32_t sequence;
    uint8_t code;
    const uint8_t *payload;
    size_t size;
};
size_t encodeCommand(uint8_t *out,size_t capacity,Command command);

// Borrowed payload is valid only during handler. No heap or Arduino dependency.
// Payload is consumed by declared length, even if it contains another magic.
class Parser {
public:
    using Handler=void(*)(void*,const PacketView&);
    Parser(Handler handler,void *context):handler_(handler),context_(context) {}
    void feed(uint8_t byte,uint32_t now);
    // A chunk may end anywhere or contain several PCM/events. Payload is copied
    // in spans; headers keep exactly the same validation and resynchronization.
    void feed(const uint8_t *bytes,size_t size,uint32_t now);
    bool expire(uint32_t now,uint32_t timeoutMs);
    uint32_t rejected() const { return rejected_; }
private:
    enum State { Search, Header, Payload, Discard } state_=Search;
    Handler handler_;
    void *context_;
    uint8_t magic_[4]={},header_[6]={},payload_[PCM_FRAME_BYTES]={};
    size_t matched_=0,used_=0;
    uint16_t length_=0;
    uint32_t lastByte_=0,rejected_=0;
    Kind kind_=Kind::Pcm;
    void reset();
    void deliver();
};
}
