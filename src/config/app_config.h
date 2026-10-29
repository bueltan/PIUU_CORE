/**
 * @file app_config.h
 * @brief Endpoint, timing and buffering configuration. Audio format is in audio_format.h;
 * server wire layout is in protocol/server_protocol.h.
 */

#pragma once

#include <stdint.h>
#include "config/audio_format.h"



// IP address of the backend server that receives uplink audio
// and sends downlink audio back to the ESP32.
constexpr const char *SERVER_IP = "74.208.158.226";

// UDP port on the backend server.
constexpr uint16_t SERVER_PORT = 9999;

// Local UDP port used by the ESP32 to bind and receive packets.
constexpr uint16_t LOCAL_UDP_PORT = 12000;

// Recommendation:
// - Keep SERVER_PORT fixed unless the backend changes.
// - LOCAL_UDP_PORT can be changed if another service already uses it.
// - For production, consider moving WiFi credentials out of source code.

// ============================================================
// Jitter buffer
// ============================================================

// Total number of packet slots reserved in the jitter buffer.
constexpr int JITTER_SLOTS = 170;

// Number of buffered packets required before playback starts the first time.
// Higher values improve smoothness but increase latency.
constexpr int START_BUFFERED_PACKETS = 50;

// Number of buffered packets required before resuming after underrun/rebuffer.
constexpr int REBUFFER_PACKETS = 40;

// Buffer threshold considered "low" during playback.
constexpr int LOW_WATER_PACKETS = 12;

// Maximum number of consecutive missing packets tolerated before
// stronger recovery or rebuffering logic is triggered.
constexpr int MAX_CONSECUTIVE_MISSES = 10;

// Largest acceptable future sequence jump relative to expectedSeq.
// Packets too far ahead are dropped to avoid runaway buffering.
constexpr uint32_t MAX_AHEAD_WINDOW = 100;

// If the sequence gap becomes larger than this during playback,
// the code may force a hard rebuffer.
constexpr uint32_t HARD_REBUFFER_GAP = 28;

// Number of consecutive misses required before enabling hard rebuffer logic.
constexpr uint32_t HARD_REBUFFER_MISSES = 10;

// Recommendation:
// - START_BUFFERED_PACKETS is one of the main latency vs smoothness controls.
// - Lower it for lower latency on stable local networks.
// - Raise it for unstable remote networks with bursty packet delivery.
// - REBUFFER_PACKETS is usually slightly lower than START_BUFFERED_PACKETS.
// - LOW_WATER_PACKETS should be low enough to avoid unnecessary rebuffering,
//   but high enough to detect underrun risk early.
// - If playback stutters on remote networks, first try increasing
//   START_BUFFERED_PACKETS and REBUFFER_PACKETS before changing more advanced thresholds.
// - JITTER_SLOTS must be large enough to hold the working window plus reordering slack.

// ============================================================
// Timing
// ============================================================

// A stalled response is aborted explicitly; only END means normal completion.
#ifndef RESPONSE_STALL_TIMEOUT_MS
#define RESPONSE_STALL_TIMEOUT_MS 10000u
#endif

// Playback task period in milliseconds.
// 20 ms matches one Opus frame at 16 kHz / 320 samples.
constexpr uint32_t PLAYBACK_PERIOD_MS = 20;

// Interval for printing runtime playback/debug statistics to the optional debug UART.
constexpr uint32_t STATS_PERIOD_MS = 2000;

// Recommendation:
// - PLAYBACK_PERIOD_MS should usually match the audio frame duration.
// - RESPONSE_STALL_TIMEOUT_MS bounds waiting for stalled audio or a lost END.
// - STATS_PERIOD_MS can be lowered while debugging and raised in normal use
//   to reduce Serial noise.
#ifndef CONVERSATION_IDLE_TIMEOUT_MS
#define CONVERSATION_IDLE_TIMEOUT_MS 10000u
#endif

#ifndef CONVERSATION_ACTIVITY_LEVEL
#define CONVERSATION_ACTIVITY_LEVEL 600u
#endif

// Initial microphone-specific tuning: mean absolute PCM amplitude, not RMS.
#ifndef CONVERSATION_ACTIVITY_HOLD_MS
#define CONVERSATION_ACTIVITY_HOLD_MS 200u
#endif

// Half-duplex microphone guard after the DAC has finished playback.
constexpr uint32_t CONVERSATION_ECHO_GUARD_MS = 250;

// Bound a provider speech-start that never receives an end, even with noise.
#ifndef CONVERSATION_SPEECH_WATCHDOG_MS
#define CONVERSATION_SPEECH_WATCHDOG_MS 120000u
#endif
