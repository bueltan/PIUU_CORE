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
// Request RX service halfway through the 128-byte hardware FIFO, leaving
// ~694 us of headroom at 921600 baud (8N1), instead of ~87 us at the default 120.
constexpr uint8_t RX_FIFO_FULL_THRESHOLD=64;
constexpr size_t RX_BUFFER_BYTES=4096, TX_BUFFER_BYTES=256, AUDIO_QUEUE_FRAMES=6, EVENT_QUEUE_ITEMS=8;
// Scratch space, not an additional audio queue. Bound work before yielding.
constexpr size_t RX_READ_CHUNK_BYTES=256, RX_WORK_BUDGET_BYTES=1024;
constexpr uint32_t WORKER_STACK_BYTES=4096;
static_assert(RX_PIN!=TX_PIN,"EARS RX and TX must differ");
}
