#pragma once
#include <atomic>
#include <stdint.h>

namespace Diagnostics {
// One RX task writes; the loop reads only atomic maxima. No allocation or logs
// on the receive path. Unsigned subtraction tolerates micros() rollover.
struct RxServiceProbe {
    std::atomic<uint32_t> idleMaxUs{0},workMaxUs{0};
    std::atomic<uint32_t> socketMaxUs{0},dispatchMaxUs{0},queueMaxUs{0};
    uint32_t started=0,finished=0;
    bool seen=false;
    static void maximum(std::atomic<uint32_t> &target,uint32_t value) {
        if(value>target.load(std::memory_order_relaxed))
            target.store(value,std::memory_order_relaxed);
    }
    void begin(uint32_t now) {
        if(seen) maximum(idleMaxUs,now-finished);
        started=now;
    }
    void end(uint32_t now) {
        maximum(workMaxUs,now-started);finished=now;seen=true;
    }
};
}
