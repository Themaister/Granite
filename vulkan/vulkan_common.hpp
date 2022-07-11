/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "intrusive.hpp"
#include "object_pool.hpp"
#include "intrusive_hash_map.hpp"
#include "vulkan_headers.hpp"

namespace Vulkan
{
#ifdef GRANITE_VULKAN_MT
using HandleCounter = Util::MultiThreadCounter;
#else
using HandleCounter = Util::SingleThreadCounter;
#endif

#ifdef GRANITE_VULKAN_MT
template <typename T>
using VulkanObjectPool = Util::ThreadSafeObjectPool<T>;
template <typename T>
using VulkanCache = Util::ThreadSafeIntrusiveHashMapReadCached<T>;
template <typename T>
using VulkanCacheReadWrite = Util::ThreadSafeIntrusiveHashMap<T>;
#else
template <typename T>
using VulkanObjectPool = Util::ObjectPool<T>;
template <typename T>
using VulkanCache = Util::IntrusiveHashMap<T>;
template <typename T>
using VulkanCacheReadWrite = Util::IntrusiveHashMap<T>;
#endif

enum QueueIndices
{
	QUEUE_INDEX_GRAPHICS,
	QUEUE_INDEX_COMPUTE,
	QUEUE_INDEX_TRANSFER,
	QUEUE_INDEX_VIDEO_DECODE,
	QUEUE_INDEX_COUNT
};

struct ExternalHandle
{
#ifdef _WIN32
	using NativeHandle = void *;
	NativeHandle handle = nullptr;
#else
	using NativeHandle = int;
	NativeHandle handle = -1;
#endif

	VkExternalMemoryHandleTypeFlagBits memory_handle_type = get_opaque_memory_handle_type();
	VkExternalSemaphoreHandleTypeFlagBits semaphore_handle_type = get_opaque_semaphore_handle_type();

	constexpr static VkExternalMemoryHandleTypeFlagBits get_opaque_memory_handle_type()
	{
#ifdef _WIN32
		return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
		return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
	}

	constexpr static VkExternalSemaphoreHandleTypeFlagBits get_opaque_semaphore_handle_type()
	{
#ifdef _WIN32
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
	}

	inline explicit operator bool() const
	{
#ifdef _WIN32
		return handle != nullptr;
#else
		return handle >= 0;
#endif
	}

	static bool memory_handle_type_imports_by_reference(VkExternalMemoryHandleTypeFlagBits type)
	{
		VK_ASSERT(type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
		          type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT ||
		          type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT ||
		          type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT ||
		          type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT);
		return type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	}

	static bool semaphore_handle_type_imports_by_reference(VkExternalSemaphoreHandleTypeFlagBits type)
	{
		VK_ASSERT(type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT ||
		          type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT ||
		          type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT);

		// D3D11 fence aliases D3D12 fence. It's basically the same thing, just D3D11.3.
		return type != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	}
};
}
