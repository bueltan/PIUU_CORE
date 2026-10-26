#pragma once
#include <stddef.h>
#include <stdint.h>

// UDP v3 envelope. All integers are big endian. Audio bodies retain the existing
// PCM/Opus serialization; naked v1 datagrams are never accepted on the network.
namespace SessionProtocol {
constexpr size_t HEADER_SIZE=38, MAX_BODY=658, MAX_PACKET=HEADER_SIZE+MAX_BODY;
constexpr uint8_t VERSION=3;
enum class Kind : uint8_t {
    Hello=1, HelloAck, Start, StartAck, Commit, CommitAck, Cancel, CancelAck,
    Pcm, Opus, Nack, Missing, Error, Ping, Pong, Event, EventAck, Interrupt, InterruptAck, Played, PlayedAck, DownlinkNack, AudioEnd, AudioEndAck,
    Eyes // 25: optional best-effort ASCII visual command, no control ACK.
};
enum class Event : uint8_t { SpeechStarted=1, SpeechStopped, ResponseStarted, Closed, Ready };
// turn is the conversation ID; response=0 identifies its continuous uplink.
struct Identity {
    uint64_t client,server;
    uint32_t turn,response;
};
struct Packet {
    Kind kind;
    uint16_t flags;
    Identity id;
    uint32_t sequence;
    const uint8_t *body;
    uint16_t size;
};
size_t encode(uint8_t*,size_t,const Packet&);
bool decode(const uint8_t*,size_t,Packet&);
bool sameSession(const Identity&,const Identity&);
bool sameTurn(const Identity&,const Identity&);
}
