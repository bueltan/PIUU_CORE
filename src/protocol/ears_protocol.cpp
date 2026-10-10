#include "protocol/ears_protocol.h"
#include <cstring>
namespace EarsProtocol {
static uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
static uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
size_t encodeCommand(uint8_t *out,size_t capacity,Command command) {
    if (!out || capacity<CONTROL_HEADER_BYTES || (command!=Command::Start && command!=Command::Stop)) return 0;
    memcpy(out,"CMD!",4);out[4]=VERSION;out[5]=uint8_t(command);out[6]=out[7]=0;
    return CONTROL_HEADER_BYTES;
}
void Parser::reset() { state_=Search;matched_=used_=0;length_=0; }
bool Parser::expire(uint32_t now,uint32_t timeoutMs) {
    if ((state_!=Search || matched_) && uint32_t(now-lastByte_)>=timeoutMs) { ++rejected_;reset();return true; }
    return false;
}
void Parser::deliver() {
    const PacketView packet{kind_,kind_==Kind::Pcm ? le32(header_) : 0,
        kind_==Kind::Event ? header_[1] : uint8_t(0),length_ ? payload_ : nullptr,length_};
    if (handler_) handler_(context_,packet);
    reset();
}
void Parser::feed(uint8_t byte,uint32_t now) {
    lastByte_=now;
    if (state_==Search) {
        // Four-byte sliding window handles overlapping PCM!/EVT! and arbitrary noise.
        if (matched_<4) magic_[matched_++]=byte;
        else { memmove(magic_,magic_+1,3);magic_[3]=byte; }
        if (matched_==4 && (!memcmp(magic_,"PCM!",4) || !memcmp(magic_,"EVT!",4))) {
            kind_=magic_[0]=='P' ? Kind::Pcm : Kind::Event;state_=Header;used_=0;matched_=0;
        }
        return;
    }
    if (state_==Header) {
        header_[used_++]=byte;
        const size_t bytes=kind_==Kind::Pcm ? 6 : 4;
        if (used_<bytes) return;
        length_=le16(header_+bytes-2);used_=0;
        const bool valid=kind_==Kind::Pcm ? length_==PCM_FRAME_BYTES : header_[0]==VERSION && length_<=MAX_EVENT_BYTES;
        if (!valid) ++rejected_;
        if (!length_) { if (valid) deliver();else reset();return; }
        state_=valid ? Payload : Discard;
        return;
    }
    if (state_==Payload) payload_[used_]=byte;
    if (++used_==length_) { if (state_==Payload) deliver();else reset(); }
}
}
