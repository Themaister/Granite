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

#include "logging.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Util
{
static thread_local LoggingInterface *logging_iface;

bool interface_log(const char *tag, const char *fmt, ...)
{
	if (!logging_iface)
		return false;

	va_list va;
	va_start(va, fmt);
	bool ret = logging_iface->log(tag, fmt, va);
	va_end(va);
	return ret;
}

void set_thread_logging_interface(LoggingInterface *iface)
{
	logging_iface = iface;
}

#ifdef _WIN32
void debug_output_log(const char *tag, const char *fmt, ...)
{
	if (!IsDebuggerPresent())
		return;

	va_list va;
	va_start(va, fmt);
	auto len = vsnprintf(nullptr, 0, fmt, va);
	if (len > 0)
	{
		size_t tag_len = strlen(tag);
		char *buf = new char[len + tag_len + 1];
		memcpy(buf, tag, tag_len);
		vsnprintf(buf + tag_len, len + 1, fmt, va);
		OutputDebugStringA(buf);
		delete[] buf;
	}
	va_end(va);
}
#endif
}