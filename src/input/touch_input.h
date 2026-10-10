#pragma once
#include <stdint.h>
enum class TouchEdge : uint8_t { Down, Up };
struct TouchEvent { TouchEdge edge; uint16_t x, y; uint32_t capturedMs; };
bool startTouchTask();
bool takeTouchEvent(TouchEvent &event);
