#pragma once
#include <stddef.h>
#define MALLOC_CAP_8BIT 0
static inline size_t heap_caps_get_free_size(int caps) { (void)caps; return (size_t)64 << 20; }
