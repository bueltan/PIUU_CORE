#pragma once
#include "diagnostics/config.h"
#include <cstddef>
#include <cstdint>

namespace WifiDiagnostics {
enum class Event : uint8_t {
    Attempt, BeginStatus, Timeout, Ready, StaStart, Associated, Disconnected,
    GotIp, LostIp, ScanStart, ScanEnd, SetupFailed
};
// Cursor belongs to the diagnostic transport owner. Zero starts replay at the oldest retained event.
bool next(uint64_t &cursor, char *line, size_t capacity, uint32_t &ms);
void record(Event event, int32_t detail = 0, uint32_t extra = 0);
void begin();
}
