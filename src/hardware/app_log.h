#pragma once
// Event logs and 5-second snapshots; independent of USB/UART log routing.
#include "diagnostics/config.h"
// UART0 provides logs over USB. An optional UART1 adapter can override it.
#ifdef PIUU_DEBUG_TX_PIN
#include <Arduino.h>
#include "config/ears_config.h"
static_assert(PIUU_DEBUG_TX_PIN!=EarsConfig::TX_PIN && PIUU_DEBUG_TX_PIN!=EarsConfig::RX_PIN,"Debug cannot share EARS UART pins");
inline HardwareSerial &appLog() { return Serial1; }
inline void appLogBegin() { Serial1.begin(115200,SERIAL_8N1,-1,PIUU_DEBUG_TX_PIN); }
#else
#include <Arduino.h>
inline decltype(Serial) &appLog() { return Serial; }
inline void appLogBegin() { Serial.begin(115200); }
#endif
