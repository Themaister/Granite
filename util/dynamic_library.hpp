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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Util
{
class DynamicLibrary
{
public:
	DynamicLibrary() = default;
	explicit DynamicLibrary(const char *path);
	~DynamicLibrary();

	DynamicLibrary(DynamicLibrary &&other) noexcept;
	DynamicLibrary &operator=(DynamicLibrary &&other) noexcept;

	template <typename Func>
	Func get_symbol(const char *symbol)
	{
		return reinterpret_cast<Func>(get_symbol_internal(symbol));
	}

	explicit operator bool() const
	{
#if _WIN32
		return module != nullptr;
#else
		return dylib != nullptr;
#endif
	}

private:
#if _WIN32
	HMODULE module = nullptr;
#else
	void *dylib = nullptr;
#endif

	void *get_symbol_internal(const char *symbol);
	void close();
};
}
