#include "diagnostics/wifi_history.h"
#if PIUU_WIFI_DIAGNOSTICS
#include "diagnostics/wifi_history_buffer.h"
#include <WiFi.h>
#include <cstdio>

namespace WifiDiagnostics {
namespace {
History history;
const char *name(Event event) {
    switch (event) {
    case Event::Attempt: return "attempt";
    case Event::BeginStatus: return "begin_status";
    case Event::Timeout: return "timeout";
    case Event::Ready: return "ready";
    case Event::StaStart: return "sta_start";
    case Event::Associated: return "associated";
    case Event::Disconnected: return "disconnected";
    case Event::GotIp: return "got_ip";
    case Event::LostIp: return "lost_ip";
    case Event::ScanStart: return "scan_start";
    case Event::ScanEnd: return "scan_end";
    case Event::SetupFailed: return "setup_failed";
    }
    return "unknown";
}
void onEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START: record(Event::StaStart); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        record(Event::Associated, info.wifi_sta_connected.channel,
               info.wifi_sta_connected.authmode); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        record(Event::Disconnected, info.wifi_sta_disconnected.reason); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        record(Event::GotIp, 0, info.got_ip.ip_info.ip.addr); break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: record(Event::LostIp); break;
    default: break;
    }
}
}
void begin() {
    // Called once by wifiServiceStart, before starting the worker/radio.
    static bool registered = false;
    if (!registered) { WiFi.onEvent(onEvent); registered = true; }
}
void record(Event event, int32_t detail, uint32_t extra) {
    history.push({millis(), event, detail, extra});
}
bool next(uint64_t &cursor, char *line, size_t capacity, uint32_t &ms) {
    // A full line must fit in one UDP log payload.
    if (!line || capacity < 192) return false;
    Entry entry{};
    uint64_t skipped = 0;
    if (!history.read(cursor, entry, skipped)) return false;
    ms = entry.ms;
    const char *fields = "status";
    const char *extraField = "extra";
    if (entry.event == Event::Ready || entry.event == Event::Timeout) extraField = "elapsed_ms";
    else if (entry.event == Event::Associated) extraField = "authmode";
    if (entry.event == Event::Disconnected) fields = "reason";
    else if (entry.event == Event::Associated) fields = "channel";
    else if (entry.event == Event::ScanEnd) fields = "networks";
    else if (entry.event == Event::SetupFailed) fields = "step";
    if (entry.event == Event::GotIp) {
        snprintf(line, capacity,
            "\n[WIFI_HISTORY t=%lu] event=got_ip ip=%u.%u.%u.%u skipped=%llu\n",
            (unsigned long)ms, unsigned(entry.extra & 255), unsigned((entry.extra >> 8) & 255),
            unsigned((entry.extra >> 16) & 255), unsigned(entry.extra >> 24),
            (unsigned long long)skipped);
    } else {
        snprintf(line, capacity,
            "\n[WIFI_HISTORY t=%lu] event=%s %s=%ld %s=%lu skipped=%llu\n",
            (unsigned long)ms, name(entry.event), fields, (long)entry.detail, extraField,
            (unsigned long)entry.extra, (unsigned long long)skipped);
    }
    return true;
}
}
#else
namespace WifiDiagnostics {
void begin() {}
void record(Event, int32_t, uint32_t) {}
bool next(uint64_t &, char *, size_t, uint32_t &) { return false; }
}
#endif
