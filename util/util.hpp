/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <sstream>
#include <vector>
#include <type_traits>

#if defined(HAVE_LIBRETRO)
#include "libretro.h"
namespace Granite
{
extern retro_log_printf_t libretro_log;
}
#define LOGE(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_ERROR, __VA_ARGS__); } while(0)
#define LOGI(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_INFO, __VA_ARGS__); } while(0)
#elif defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define LOGE(...) do { \
    fprintf(stderr, "[ERROR]: " __VA_ARGS__); \
    fflush(stderr); \
    char buffer[4096]; \
    sprintf(buffer, "[ERROR]: " __VA_ARGS__); \
    OutputDebugStringA(buffer); \
} while(false)
#define LOGI(...) do { \
    fprintf(stderr, "[INFO]: " __VA_ARGS__); \
    fflush(stderr); \
    char buffer[4096]; \
    sprintf(buffer, "[INFO]: " __VA_ARGS__); \
    OutputDebugStringA(buffer); \
} while(false)
#elif defined(ANDROID)
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Granite", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Granite", __VA_ARGS__)
#else
#define LOGE(...)                     \
    do                                \
    {                                 \
        fprintf(stderr, "[ERROR]: " __VA_ARGS__); \
        fflush(stderr); \
    } while (false)

#define LOGI(...)                     \
    do                                \
    {                                 \
        fprintf(stderr, "[INFO]: " __VA_ARGS__); \
        fflush(stderr); \
    } while (false)
#endif

#define STRINGIFY(x) #x

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace Util
{
#ifdef __GNUC__
#define leading_zeroes(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define trailing_zeroes(x) ((x) == 0 ? 32 : __builtin_ctz(x))
#define trailing_ones(x) __builtin_ctz(~(x))
#elif defined(_MSC_VER)
namespace Internal
{
static inline uint32_t clz(uint32_t x)
{
	unsigned long result;
	if (_BitScanReverse(&result, x))
		return 31 - result;
	else
		return 32;
}

static inline uint32_t ctz(uint32_t x)
{
	unsigned long result;
	if (_BitScanForward(&result, x))
		return result;
	else
		return 32;
}
}

#define leading_zeroes(x) ::Util::Internal::clz(x)
#define trailing_zeroes(x) ::Util::Internal::ctz(x)
#define trailing_ones(x) ::Util::Internal::ctz(~(x))
#else
#error "Implement me."
#endif

template<typename T>
inline void for_each_bit(uint32_t value, const T &func)
{
	while (value)
	{
		uint32_t bit = trailing_zeroes(value);
		func(bit);
		value &= ~(1u << bit);
	}
}

template<typename T>
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

namespace inner
{
template<typename T>
void join_helper(std::ostringstream &stream, T &&t)
{
	stream << std::forward<T>(t);
}

template<typename T, typename... Ts>
void join_helper(std::ostringstream &stream, T &&t, Ts &&... ts)
{
	stream << std::forward<T>(t);
	join_helper(stream, std::forward<Ts>(ts)...);
}
}

template<typename... Ts>
std::string join(Ts &&... ts)
{
	std::ostringstream stream;
	inner::join_helper(stream, std::forward<Ts>(ts)...);
	return stream.str();
}

std::vector<std::string> split(const std::string &str, const char *delim);
std::vector<std::string> split_no_empty(const std::string &str, const char *delim);
std::string strip_whitespace(const std::string &str);

}
