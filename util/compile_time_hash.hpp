/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

namespace Util
{
#ifdef _MSC_VER
// MSVC generates bogus warnings here.
#pragma warning(disable: 4307)
#endif

constexpr uint64_t fnv_iterate(uint64_t hash, uint8_t c)
{
	return (hash * 0x100000001b3ull) ^ c;
}

template<size_t index>
constexpr uint64_t compile_time_fnv1_inner(uint64_t hash, const char *str)
{
	return compile_time_fnv1_inner<index - 1>(fnv_iterate(hash, uint8_t(str[index])), str);
}

template<>
constexpr uint64_t compile_time_fnv1_inner<size_t(-1)>(uint64_t hash, const char *)
{
	return hash;
}

template<size_t len>
constexpr uint64_t compile_time_fnv1(const char (&str)[len])
{
	return compile_time_fnv1_inner<len - 1>(0xcbf29ce484222325ull, str);
}

constexpr uint64_t compile_time_fnv1_merge(uint64_t a, uint64_t b)
{
	return fnv_iterate(
			fnv_iterate(
					fnv_iterate(
							fnv_iterate(
									fnv_iterate(
											fnv_iterate(
													fnv_iterate(
															fnv_iterate(a, uint8_t(b >> 0)),
															uint8_t(b >> 8)),
													uint8_t(b >> 16)),
											uint8_t(b >> 24)),
									uint8_t(b >> 32)),
							uint8_t(b >> 40)),
					uint8_t(b >> 48)),
			uint8_t(b >> 56));
}

constexpr uint64_t compile_time_fnv1_merged(uint64_t hash)
{
	return hash;
}

template <typename T, typename... Ts>
constexpr uint64_t compile_time_fnv1_merged(T hash, T hash2, Ts... hashes)
{
	return compile_time_fnv1_merged(compile_time_fnv1_merge(hash, hash2), hashes...);
}
}