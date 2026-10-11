#pragma once
#include <stdint.h>
enum class PlaybackEventKind : uint8_t { Started, Finished, Failed };
struct PlaybackEvent { uint32_t epoch; PlaybackEventKind kind; };
using PlaybackEventSink = bool (*)(PlaybackEvent event);
