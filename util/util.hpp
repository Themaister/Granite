#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <sstream>

#define LOG(...)                      \
	do                                \
	{                                 \
		fprintf(stderr, __VA_ARGS__); \
	} while (0)

#define STRINGIFY(x) #x

namespace Util
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

std::string read_file_to_string(const std::string &path);

namespace inner
{
template <typename T>
void join_helper(std::ostringstream &stream, T &&t)
{
	stream << std::forward<T>(t);
}

template <typename T, typename... Ts>
void join_helper(std::ostringstream &stream, T &&t, Ts &&... ts)
{
	stream << std::forward<T>(t);
	join_helper(stream, std::forward<Ts>(ts)...);
}
}

template <typename... Ts>
std::string join(Ts &&... ts)
{
	std::ostringstream stream;
	inner::join_helper(stream, std::forward<Ts>(ts)...);
	return stream.str();
}

}
