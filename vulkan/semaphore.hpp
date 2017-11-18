/* Copyright (c) 2017 Hans-Kristian Arntzen
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
#include "vulkan.hpp"
#include "cookie.hpp"

namespace Vulkan
{
class Device;

class SemaphoreHolder : public Util::IntrusivePtrEnabled<SemaphoreHolder>, public InternalSyncEnabled
{
public:
	SemaphoreHolder(Device *device, VkSemaphore semaphore, bool signalled)
	    : device(device)
	    , semaphore(semaphore)
	    , signalled(signalled)
	{
	}

	~SemaphoreHolder();

	const VkSemaphore &get_semaphore() const
	{
		return semaphore;
	}

	bool is_signalled() const
	{
		return signalled;
	}

	VkSemaphore consume()
	{
		auto ret = semaphore;
		VK_ASSERT(semaphore);
		VK_ASSERT(signalled);
		semaphore = VK_NULL_HANDLE;
		signalled = false;
		return ret;
	}

	bool can_recycle() const
	{
		return !should_destroy_on_consume;
	}

	void signal_external()
	{
		VK_ASSERT(!signalled);
		VK_ASSERT(semaphore);
		signalled = true;
	}

	void destroy_on_consume()
	{
		should_destroy_on_consume = true;
	}

private:
	Device *device;
	VkSemaphore semaphore;
	bool signalled = true;
	bool should_destroy_on_consume = false;
};

using Semaphore = Util::IntrusivePtr<SemaphoreHolder>;
}
