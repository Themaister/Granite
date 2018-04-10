#include "dynamic_library.hpp"
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
		throw std::runtime_error("Failed to load dynamic library.");
#else
	dylib = dlopen(path, RTLD_NOW);
	if (!dylib)
		throw std::runtime_error("Failed to load dynamic library.");
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
		return GetProcAddress(module, symbol);
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