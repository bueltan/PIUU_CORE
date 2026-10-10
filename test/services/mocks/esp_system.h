#pragma once
#include <stdint.h>
inline uint32_t esp_random() { static uint32_t n=1;return ++n; }
