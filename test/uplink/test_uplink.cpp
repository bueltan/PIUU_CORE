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
    assert(!audioUplinkBeginTurn());
    audioUplinkSetSender(capture);
    assert(audioUplinkBeginTurn());
    assert(!audioUplinkBeginTurn());
    assert(!audioUplinkSubmitFrame(pcm,319));
    assert(audioUplinkSubmitFrame(pcm,320));
    assert(sent[0].size()==658 && !memcmp(sent[0].data(),"PCM!",4));
    const uint8_t expected[] = {1,0, 0,0,0,0, 0,0,0,0, 1,64, 2,128};
    assert(!memcmp(sent[0].data()+4,expected,sizeof(expected)));
    assert(!memcmp(sent[0].data()+18,pcm,640));
    audioUplinkEndTurn();
    assert(!audioUplinkSubmitFrame(pcm,320));
    assert(!audioUplinkBeginTurn());
    audioUplinkPoll(); // first END immediately
    now=11; audioUplinkPoll(); assert(sent.size()==2);
    now=12; audioUplinkPoll();
    now=24; audioUplinkPoll(); assert(sent.size()==4);
    for (size_t i=1;i<4;++i) {
        assert(sent[i].size()==18 && sent[i][5]==1);
        assert(sent[i][9]==1 && sent[i][12]==1 && sent[i][13]==64);
    }
    now=103; audioUplinkPoll(); assert(sent.size()==4);
    now=104; audioUplinkPoll(); assert(sent.size()==5);
    assert(sent[4].size()==6 && !memcmp(sent[4].data(),"COMMIT",6));
    audioUplinkPoll(); assert(sent.size()==5);
    assert(audioUplinkBeginTurn());
    assert(audioUplinkSubmitFrame(pcm,320));
    assert(sent[5][9]==0 && sent[5][12]==0 && sent[5][13]==0);
    audioUplinkEndTurn();audioUplinkPoll();
    const size_t beforeAbort=sent.size();
    audioUplinkAbortTurn();assert(!audioUplinkIsClosing());
    now+=10000;audioUplinkPoll();assert(sent.size()==beforeAbort);
    assert(!audioUplinkSubmitFrame(pcm,320));assert(audioUplinkBeginTurn());
    audioUplinkAbortTurn();
}
