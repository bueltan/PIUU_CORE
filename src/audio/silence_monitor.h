#pragma once
#include <stdint.h>
#include <stddef.h>

// Inactivity aid, not speech recognition: measures mean absolute PCM amplitude.
// No floats, allocation, extra task or modification of samples sent to xAI.
class SilenceMonitor {
public:
    void reset(uint32_t now) { lastActivity_=now; level_=0; }
    void activity(uint32_t now) { lastActivity_=now; }
    void observe(const int16_t *samples,size_t count,uint32_t now,uint16_t threshold) {
        if(!samples || !count) return;
        uint32_t sum=0;
        for(size_t i=0;i<count;++i) {
            const int32_t value=samples[i];sum+=uint32_t(value<0 ? -value : value);
        }
        level_=uint16_t(sum/count);
        if(level_>=threshold) lastActivity_=now;
    }
    uint32_t quietMs(uint32_t now) const { return uint32_t(now-lastActivity_); }
    uint16_t level() const { return level_; }
private:
    uint32_t lastActivity_=0;
    uint16_t level_=0;
};
