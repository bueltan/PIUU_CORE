#pragma once
#include "diagnostics/config.h"
#include <stdint.h>

namespace Diagnostics {
enum class Operation { Decode, OutputWrite };
#if PIUU_PERFORMANCE_DIAGNOSTICS
void performancePoll(); // loop owns snapshots and all printing; no extra worker.
class Measure {
public:
    explicit Measure(Operation operation);
    ~Measure();
    Measure(const Measure&)=delete;
    Measure& operator=(const Measure&)=delete;
private:
    Operation operation_;
    uint32_t start_;
};
#else
inline void performancePoll() {}
class Measure {
public:
    explicit Measure(Operation) {}
};
#endif
}
