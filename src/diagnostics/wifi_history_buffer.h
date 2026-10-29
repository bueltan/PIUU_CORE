#pragma once
#include "diagnostics/wifi_history.h"
#include <mutex>

namespace WifiDiagnostics {
struct Entry {
    uint32_t ms;
    Event event;
    int32_t detail;
    uint32_t extra;
};
// Fixed event storage: no formatting or socket work in the Wi-Fi callback.
class History {
public:
    static constexpr size_t Capacity = 64;
    void push(const Entry &entry) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_[(sequence_ - 1) % Capacity] = entry;
        ++sequence_;
    }
    bool read(uint64_t &cursor, Entry &entry, uint64_t &skipped) {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t oldest = sequence_ > Capacity ? sequence_ - Capacity : 1;
        skipped = 0;
        if (!cursor) cursor = oldest;
        if (cursor < oldest) { skipped = oldest - cursor; cursor = oldest; }
        if (cursor >= sequence_) return false;
        entry = entries_[(cursor - 1) % Capacity];
        ++cursor;
        return true;
    }
private:
    std::mutex mutex_;
    Entry entries_[Capacity]{};
    uint64_t sequence_ = 1;
};
}
