/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#include "thread_name.hpp"

#if !defined(_WIN32)
#include <pthread.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#endif

namespace Util
{
void set_current_thread_name(const char *name)
{
#if defined(__linux__)
	pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
	pthread_setname_np(name);
#elif defined(_WIN32)
	using PFN_SetThreadDescription = HRESULT (WINAPI *)(HANDLE, PCWSTR);
	auto module = GetModuleHandleA("kernel32.dll");
	PFN_SetThreadDescription SetThreadDescription = module ? reinterpret_cast<PFN_SetThreadDescription>(
	    (void *)GetProcAddress(module, "SetThreadDescription")) : nullptr;

	if (SetThreadDescription)
	{
		std::wstring wname;
		while (*name != '\0')
		{
			wname.push_back(*name);
			name++;
		}
		SetThreadDescription(GetCurrentThread(), wname.c_str());
	}
#endif
}
}
