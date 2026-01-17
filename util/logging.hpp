/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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
#include <string.h>
#include <stdarg.h>

namespace Util
{
class LoggingInterface
{
public:
	virtual ~LoggingInterface() = default;
	virtual bool log(const char *tag, const char *fmt, va_list va) = 0;
};

bool interface_log(const char *tag, const char *fmt, ...);
void set_thread_logging_interface(LoggingInterface *iface);
}

#if defined(_WIN32)
namespace Util
{
void debug_output_log(const char *tag, const char *fmt, ...);
}

#define LOGE_FALLBACK(...) do { \
	fprintf(stderr, "[ERROR]: " __VA_ARGS__); \
	fflush(stderr); \
	::Util::debug_output_log("[ERROR]: ", __VA_ARGS__); \
} while(false)

#define LOGW_FALLBACK(...) do { \
	fprintf(stderr, "[WARN]: " __VA_ARGS__); \
	fflush(stderr); \
	::Util::debug_output_log("[WARN]: ", __VA_ARGS__); \
} while(false)

#define LOGI_FALLBACK(...) do { \
	fprintf(stderr, "[INFO]: " __VA_ARGS__); \
	fflush(stderr); \
	::Util::debug_output_log("[INFO]: ", __VA_ARGS__); \
} while(false)
#elif defined(ANDROID)
#include <android/log.h>
#define LOGE_FALLBACK(...) do { __android_log_print(ANDROID_LOG_ERROR, "Granite", __VA_ARGS__); } while(0)
#define LOGW_FALLBACK(...) do { __android_log_print(ANDROID_LOG_WARN, "Granite", __VA_ARGS__); } while(0)
#define LOGI_FALLBACK(...) do { __android_log_print(ANDROID_LOG_INFO, "Granite", __VA_ARGS__); } while(0)
#else
#define LOGE_FALLBACK(...)                        \
	do                                            \
	{                                             \
		fprintf(stderr, "[ERROR]: " __VA_ARGS__); \
		fflush(stderr);                           \
	} while (false)

#define LOGW_FALLBACK(...)                       \
	do                                           \
	{                                            \
		fprintf(stderr, "[WARN]: " __VA_ARGS__); \
		fflush(stderr);                          \
	} while (false)

#define LOGI_FALLBACK(...)                       \
	do                                           \
	{                                            \
		fprintf(stderr, "[INFO]: " __VA_ARGS__); \
		fflush(stderr);                          \
	} while (false)
#endif

#ifndef _MSC_VER
#include <stdio.h>

namespace Internal
{
// Stole idea from RenderDoc.
struct IsDebugged
{
	IsDebugged()
	{
		FILE *f = ::fopen("/proc/self/status", "r");
		if (!f)
			return;

		while (!feof(f))
		{
			constexpr int size = 512;
			char line[size];
			line[size - 1] = '\0';
			if (!fgets(line, sizeof(line) - 1, f))
				break;

			int pid = 0;
			if (sscanf(line, "TracerPid: %d", &pid) == 1 && pid != 0)
			{
				state = true;
				break;
			}
		}

		::fclose(f);
	}

	bool state = false;
};

static inline bool is_debugged()
{
	static IsDebugged is_debugged;
	return is_debugged.state;
}
}
#else
#define NOMINMAX
#include <windows.h>
#include <debugapi.h>
#endif

static inline void debug_break()
{
#ifdef _MSC_VER
	if (IsDebuggerPresent())
		__debugbreak();
#else
#if defined(__GNUC__) && defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
	// __builtin_trap on GCC is SIGILL, not SIGTRAP.
	// Stole idea from RenderDoc.
	if (Internal::is_debugged())
		__asm__ volatile("int $0x03;");
#elif defined(__clang__)
	if (Internal::is_debugged())
		__builtin_debugtrap();
#endif
#endif
}

#define LOGE(...) do { if (!::Util::interface_log("[ERROR]: ", __VA_ARGS__)) { LOGE_FALLBACK(__VA_ARGS__); } debug_break(); } while(0)
#define LOGW(...) do { if (!::Util::interface_log("[WARN]: ", __VA_ARGS__)) { LOGW_FALLBACK(__VA_ARGS__); }} while(0)
#define LOGI(...) do { if (!::Util::interface_log("[INFO]: ", __VA_ARGS__)) { LOGI_FALLBACK(__VA_ARGS__); }} while(0)

