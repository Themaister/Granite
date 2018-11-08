/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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
#include "vulkan.hpp"
#include "object_pool.hpp"

namespace Vulkan
{
class Device;

class FenceHolder;
struct FenceHolderDeleter
{
	void operator()(FenceHolder *fence);
};

class FenceHolder : public Util::IntrusivePtrEnabled<FenceHolder, FenceHolderDeleter, HandleCounter>
{
public:
	friend struct FenceHolderDeleter;

	~FenceHolder();
	void wait();

	bool wait_timeout(uint64_t nsec);

	VkFence get_fence() const;

private:
	friend class Util::ObjectPool<FenceHolder>;
	FenceHolder(Device *device, VkFence fence) : device(device), fence(fence)
	{
	}

	Device *device;
	VkFence fence;
};

using Fence = Util::IntrusivePtr<FenceHolder>;
}
