#pragma once
#include <cstdint>
inline uint32_t esp_random() { return 0x1234abcd; }
inline int esp_reset_reason() { return 12; }
