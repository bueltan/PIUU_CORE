#include "audio/audio_uplink.h"
#include <cassert>
#include <cstring>
#include <vector>
static uint32_t now = 0;
uint32_t millis() { return now; }
TestSerial Serial;
static std::vector<std::vector<uint8_t>> sent;
static bool capture(const uint8_t *p, size_t n) { sent.emplace_back(p,p+n); return true; }
int main() {
    int16_t pcm[320] = {123};
    assert(!audioUplinkBeginConversation());
    audioUplinkSetSender(capture);
    assert(audioUplinkBeginConversation());
    assert(!audioUplinkBeginConversation());
    assert(!audioUplinkSubmitFrame(pcm,319));
    assert(audioUplinkSubmitFrame(pcm,320));
    assert(sent[0].size()==658 && !memcmp(sent[0].data(),"PCM!",4));
    const uint8_t expected[] = {1,0, 0,0,0,0, 0,0,0,0, 1,64, 2,128};
    assert(!memcmp(sent[0].data()+4,expected,sizeof(expected)));
    assert(!memcmp(sent[0].data()+18,pcm,640));
    // Input sequence is continuous across assistant responses, until explicit close.
    audioUplinkSkipFrames(2);
    assert(audioUplinkSubmitFrame(pcm,320));assert(sent.back()[9]==3);
    audioUplinkClose();assert(!audioUplinkSubmitFrame(pcm,320));
    assert(audioUplinkBeginConversation());assert(audioUplinkSubmitFrame(pcm,320));
    assert(sent.back()[9]==0);audioUplinkClose();
}
