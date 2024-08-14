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

#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#if defined(__APPLE__) && !defined(VK_USE_PLATFORM_METAL_EXT)
#define VK_USE_PLATFORM_METAL_EXT
#endif

#if defined(VULKAN_H_) || defined(VULKAN_CORE_H_)
#error "Must include vulkan_headers.hpp before Vulkan headers"
#endif

#include "volk.h"
#include <stdlib.h>
#include "logging.hpp"
#include <utility>

// Workaround silly Xlib headers that define macros for these globally :(
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif

#ifdef VULKAN_DEBUG
#define VK_ASSERT(x)                                             \
	do                                                           \
	{                                                            \
		if (!bool(x))                                            \
		{                                                        \
			LOGE("Vulkan error at %s:%d.\n", __FILE__, __LINE__); \
			abort();                                        \
		}                                                        \
	} while (0)
#else
#define VK_ASSERT(x) ((void)0)
#endif

namespace Vulkan
{
struct NoCopyNoMove
{
	NoCopyNoMove() = default;
	NoCopyNoMove(const NoCopyNoMove &) = delete;
	void operator=(const NoCopyNoMove &) = delete;
};
}
