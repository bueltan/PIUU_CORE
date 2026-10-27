#pragma once
// Event logs and 5-second snapshots; independent of USB/UART log routing.
#include "diagnostics/config.h"
// UART0 variants mute logs; an optional UART1 adapter can override routing.
#ifdef PIUU_DEBUG_TX_PIN
#include <Arduino.h>
#include "config/ears_config.h"
static_assert(PIUU_DEBUG_TX_PIN!=EarsConfig::TX_PIN && PIUU_DEBUG_TX_PIN!=EarsConfig::RX_PIN,"Debug cannot share EARS UART pins");
inline HardwareSerial &appLog() { return Serial1; }
inline void appLogBegin() { Serial1.begin(115200,SERIAL_8N1,-1,PIUU_DEBUG_TX_PIN); }
#elif PIUU_EARS_UART0
// UART0 is the binary EARS link. Do not initialize it or inject log text here.
#include <Arduino.h>
#include <esp_log.h>
class SilentAppLog : public Print {
public:
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t *,size_t n) override { return n; }
};
inline SilentAppLog &appLog() { static SilentAppLog sink;return sink; }
inline void appLogBegin() { esp_log_level_set("*", ESP_LOG_NONE); }
#else
#include <Arduino.h>
inline decltype(Serial) &appLog() { return Serial; }
inline void appLogBegin() { Serial.begin(115200); }
#endif
