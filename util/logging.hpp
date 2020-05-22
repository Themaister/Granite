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
#include <stdio.h>

#ifdef GRANITE_LOGGING_QUEUE
#include "global_managers.hpp"
#include "message_queue.hpp"
#include <string.h>
#include <stdarg.h>

namespace Util
{
static inline void queued_log(const char *tag, const char *fmt, ...)
{
	auto *message_queue = ::Granite::Global::message_queue();
	if (!message_queue || !message_queue->is_uncorked())
		return;

	char message_buffer[16 * 1024];
	memcpy(message_buffer, tag, strlen(tag));
	va_list va;
	va_start(va, fmt);
	vsnprintf(message_buffer + strlen(tag), sizeof(message_buffer) - strlen(tag), fmt, va);
	va_end(va);

	size_t message_size = strlen(message_buffer) + 1;

	while (message_size >= 2 && message_buffer[message_size - 2] == '\n')
	{
		message_buffer[message_size - 2] = '\0';
		message_size--;
	}

	auto message_payload = message_queue->allocate_write_payload(message_size);
	if (message_payload)
	{
		memcpy(static_cast<char *>(message_payload.get_payload_data()), message_buffer, message_size);
		message_queue->push_written_payload(std::move(message_payload));
	}
}
}

#define QUEUED_LOGE(...) do { \
	::Util::queued_log("[ERROR]: ", __VA_ARGS__); \
} while(0)
#define QUEUED_LOGW(...) do { \
	::Util::queued_log("[WARN]: ", __VA_ARGS__); \
} while(0)
#define QUEUED_LOGI(...) do { \
	::Util::queued_log("[INFO]: ", __VA_ARGS__); \
} while(0)
#else
#define QUEUED_LOGE(...)
#define QUEUED_LOGW(...)
#define QUEUED_LOGI(...)
#endif

#if defined(HAVE_LIBRETRO)
#include "libretro.h"
namespace Granite
{
extern retro_log_printf_t libretro_log;
}
#define LOGE(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_ERROR, __VA_ARGS__); QUEUED_LOGE(__VA_ARGS__); } while(0)
#define LOGW(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_WARN, __VA_ARGS__); QUEUED_LOGW(__VA_ARGS__); } while(0)
#define LOGI(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_INFO, __VA_ARGS__); QUEUED_LOGI(__VA_ARGS__); } while(0)
#elif defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define LOGE(...) do { \
    fprintf(stderr, "[ERROR]: " __VA_ARGS__); \
    fflush(stderr); \
    char buffer[16 * 1024]; \
    snprintf(buffer, sizeof(buffer), "[ERROR]: " __VA_ARGS__); \
    OutputDebugStringA(buffer); \
    QUEUED_LOGE(__VA_ARGS__); \
} while(false)

#define LOGW(...) do { \
    fprintf(stderr, "[WARN]: " __VA_ARGS__); \
    fflush(stderr); \
    char buffer[16 * 1024]; \
    snprintf(buffer, sizeof(buffer), "[WARN]: " __VA_ARGS__); \
    OutputDebugStringA(buffer); \
    QUEUED_LOGW(__VA_ARGS__); \
} while(false)

#define LOGI(...) do { \
    fprintf(stderr, "[INFO]: " __VA_ARGS__); \
    fflush(stderr); \
    char buffer[16 * 1024]; \
    snprintf(buffer, sizeof(buffer), "[INFO]: " __VA_ARGS__); \
    OutputDebugStringA(buffer); \
    QUEUED_LOGI(__VA_ARGS__); \
} while(false)
#elif defined(ANDROID)
#include <android/log.h>
#define LOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, "Granite", __VA_ARGS__); QUEUED_LOGE(__VA_ARGS__); } while(0)
#define LOGW(...) do { __android_log_print(ANDROID_LOG_WARN, "Granite", __VA_ARGS__); QUEUED_LOGW(__VA_ARGS__); } while(0)
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, "Granite", __VA_ARGS__); QUEUED_LOGI(__VA_ARGS__); } while(0)
#else
#define LOGE(...)                                 \
	do                                            \
	{                                             \
		fprintf(stderr, "[ERROR]: " __VA_ARGS__); \
		fflush(stderr);                           \
		QUEUED_LOGE(__VA_ARGS__);                 \
	} while (false)

#define LOGW(...)                                \
	do                                           \
	{                                            \
		fprintf(stderr, "[WARN]: " __VA_ARGS__); \
		fflush(stderr);                          \
		QUEUED_LOGW(__VA_ARGS__);                \
	} while (false)

#define LOGI(...)                                \
	do                                           \
	{                                            \
		fprintf(stderr, "[INFO]: " __VA_ARGS__); \
		fflush(stderr);                          \
		QUEUED_LOGI(__VA_ARGS__);                \
	} while (false)
#endif
