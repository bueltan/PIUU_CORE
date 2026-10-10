#pragma once
#include <stdint.h>
#include <stddef.h>
// UART2 uses the GPIO freed by removing the RGB LED. UART0 stays on USB.
#ifndef PIUU_EARS_RX_PIN
#define PIUU_EARS_RX_PIN 16
#endif
#ifndef PIUU_EARS_TX_PIN
#define PIUU_EARS_TX_PIN 17
#endif
namespace EarsConfig {
constexpr int RX_PIN=PIUU_EARS_RX_PIN, TX_PIN=PIUU_EARS_TX_PIN;
constexpr uint32_t BAUD=921600, RX_TIMEOUT_MS=100, COMMAND_REFRESH_MS=500, PEER_TIMEOUT_MS=2500;
constexpr size_t RX_BUFFER_BYTES=4096, TX_BUFFER_BYTES=256, AUDIO_QUEUE_FRAMES=6, EVENT_QUEUE_ITEMS=8;
constexpr uint32_t WORKER_STACK_BYTES=4096;
static_assert(RX_PIN!=TX_PIN,"EARS RX and TX must differ");
}
