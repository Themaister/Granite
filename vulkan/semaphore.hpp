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

#include "vulkan_common.hpp"
#include "vulkan_headers.hpp"
#include "cookie.hpp"
#include "object_pool.hpp"

namespace Vulkan
{
class Device;

class SemaphoreHolder;
struct SemaphoreHolderDeleter
{
	void operator()(SemaphoreHolder *semaphore);
};

class SemaphoreHolder : public Util::IntrusivePtrEnabled<SemaphoreHolder, SemaphoreHolderDeleter, HandleCounter>,
                        public InternalSyncEnabled
{
public:
	friend struct SemaphoreHolderDeleter;

	~SemaphoreHolder();

	const VkSemaphore &get_semaphore() const
	{
		return semaphore;
	}

	bool is_signalled() const
	{
		return signalled;
	}

	uint64_t get_timeline_value() const
	{
		VK_ASSERT(!owned && semaphore_type == VK_SEMAPHORE_TYPE_TIMELINE_KHR);
		return timeline;
	}

	VkSemaphore consume()
	{
		auto ret = semaphore;
		VK_ASSERT(semaphore);
		VK_ASSERT(signalled);
		semaphore = VK_NULL_HANDLE;
		signalled = false;
		owned = false;
		return ret;
	}

	VkSemaphore release_semaphore()
	{
		auto ret = semaphore;
		semaphore = VK_NULL_HANDLE;
		signalled = false;
		owned = false;
		return ret;
	}

	void wait_external()
	{
		VK_ASSERT(semaphore);
		VK_ASSERT(signalled);
		signalled = false;
	}

	void signal_external()
	{
		VK_ASSERT(!signalled);
		VK_ASSERT(semaphore);
		signalled = true;
	}

	void set_pending_wait()
	{
		pending_wait = true;
	}

	bool is_pending_wait() const
	{
		return pending_wait;
	}

	void set_external_object_compatible(VkExternalSemaphoreHandleTypeFlagBits handle_type,
	                                    VkExternalSemaphoreFeatureFlags features)
	{
		external_compatible_handle_type = handle_type;
		external_compatible_features = features;
	}

	bool is_external_object_compatible() const
	{
		return external_compatible_features != 0;
	}

	VkSemaphoreTypeKHR get_semaphore_type() const
	{
		return semaphore_type;
	}

	bool is_proxy_timeline() const
	{
		return proxy_timeline;
	}

	void set_proxy_timeline()
	{
		proxy_timeline = true;
		signalled = false;
	}

	// If successful, importing takes ownership of the handle/fd.
	// Application can use dup() / DuplicateHandle() to keep a reference.
	// Imported semaphores are assumed to be signalled, or pending to be signalled.
	// All imports are performed with TEMPORARY permanence.
	ExternalHandle export_to_handle();
	bool import_from_handle(ExternalHandle handle);

	VkExternalSemaphoreFeatureFlags get_external_features() const
	{
		return external_compatible_features;
	}

	VkExternalSemaphoreHandleTypeFlagBits get_external_handle_type() const
	{
		return external_compatible_handle_type;
	}

	SemaphoreHolder &operator=(SemaphoreHolder &&other) noexcept;

private:
	friend class Util::ObjectPool<SemaphoreHolder>;
	SemaphoreHolder(Device *device_, VkSemaphore semaphore_, bool signalled_, bool owned_)
		: device(device_)
		, semaphore(semaphore_)
		, timeline(0)
		, semaphore_type(VK_SEMAPHORE_TYPE_BINARY_KHR)
		, signalled(signalled_)
		, owned(owned_)
	{
	}

	SemaphoreHolder(Device *device_, uint64_t timeline_, VkSemaphore semaphore_, bool owned_)
		: device(device_)
		, semaphore(semaphore_)
		, timeline(timeline_)
		, semaphore_type(VK_SEMAPHORE_TYPE_TIMELINE_KHR)
		, owned(owned_)
	{
		VK_ASSERT((owned && timeline == 0) || (!owned && timeline != 0));
	}

	explicit SemaphoreHolder(Device *device_)
		: device(device_)
	{
	}

	void recycle_semaphore();

	Device *device;
	VkSemaphore semaphore = VK_NULL_HANDLE;
	uint64_t timeline = 0;
	VkSemaphoreTypeKHR semaphore_type = VK_SEMAPHORE_TYPE_BINARY_KHR;
	bool signalled = false;
	bool pending_wait = false;
	bool owned = false;
	bool proxy_timeline = false;
	VkExternalSemaphoreHandleTypeFlagBits external_compatible_handle_type = {};
	VkExternalSemaphoreFeatureFlags external_compatible_features = 0;
};

using Semaphore = Util::IntrusivePtr<SemaphoreHolder>;
}
