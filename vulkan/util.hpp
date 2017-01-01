#pragma once

#include <stdint.h>

namespace Vulkan
{
#ifdef __GNUC__
#define leading_zeroes(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define trailing_zeroes(x) ((x) == 0 ? 32 : __builtin_ctz(x))
#define trailing_ones(x) __builtin_ctz(~(x))
#else
#error "Implement me."
#endif

template <typename T>
inline void for_each_bit(uint32_t value, const T &func)
{
	while (value)
	{
		uint32_t bit = trailing_zeroes(value);
		func(bit);
		value &= ~(1u << bit);
	}
}

template <typename T>
inline void for_each_bit_range(uint32_t value, const T &func)
{
	while (value)
	{
		uint32_t bit = trailing_zeroes(value);
		uint32_t range = trailing_ones(value >> bit);
		func(bit, range);
		value &= ~((1u << (bit + range)) - 1);
	}
}

inline uint32_t next_pow2(uint32_t v)
{
	v--;
	v |= v >> 16;
	v |= v >> 8;
	v |= v >> 4;
	v |= v >> 2;
	v |= v >> 1;
	return v + 1;
}
}
