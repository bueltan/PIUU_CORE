#pragma once
constexpr int MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
inline unsigned heap_caps_get_free_size(int) { return 45678; }
inline unsigned heap_caps_get_largest_free_block(int) { return 20000; }
