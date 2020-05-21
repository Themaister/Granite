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

#include "dynamic_library.hpp"
#include "logging.hpp"
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#else
#include <dlfcn.h>
#endif

namespace Util
{
DynamicLibrary::DynamicLibrary(const char *path)
{
#ifdef _WIN32
	module = LoadLibrary(path);
	if (!module)
		LOGE("Failed to load dynamic library.\n");
#else
	dylib = dlopen(path, RTLD_NOW);
	if (!dylib)
		LOGE("Failed to load dynamic library.\n");
#endif
}

DynamicLibrary::DynamicLibrary(Util::DynamicLibrary &&other) noexcept
{
	*this = std::move(other);
}

DynamicLibrary &DynamicLibrary::operator=(Util::DynamicLibrary &&other) noexcept
{
	close();
#ifdef _WIN32
	module = other.module;
	other.module = nullptr;
#else
	dylib = other.dylib;
	other.dylib = nullptr;
#endif
	return *this;
}

void DynamicLibrary::close()
{
#ifdef _WIN32
	if (module)
		FreeLibrary(module);
	module = nullptr;
#else
	if (dylib)
		dlclose(dylib);
	dylib = nullptr;
#endif
}

DynamicLibrary::~DynamicLibrary()
{
	close();
}

void *DynamicLibrary::get_symbol_internal(const char *symbol)
{
#ifdef _WIN32
	if (module)
		return (void*)GetProcAddress(module, symbol);
	else
		return nullptr;
#else
	if (dylib)
		return dlsym(dylib, symbol);
	else
		return nullptr;
#endif
}
}
