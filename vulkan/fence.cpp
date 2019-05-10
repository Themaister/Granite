/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

#include "fence.hpp"
#include "device.hpp"

namespace Vulkan
{
FenceHolder::~FenceHolder()
{
	if (fence != VK_NULL_HANDLE)
		device->reset_fence(fence, observed_wait);
}

VkFence FenceHolder::get_fence() const
{
	return fence;
}

void FenceHolder::wait()
{
	auto &table = device->get_device_table();
	if (table.vkWaitForFences(device->get_device(), 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		LOGE("Failed to wait for fence!\n");
	else
		observed_wait = true;
}

bool FenceHolder::wait_timeout(uint64_t timeout)
{
	auto &table = device->get_device_table();
	bool ret = table.vkWaitForFences(device->get_device(), 1, &fence, VK_TRUE, timeout) == VK_SUCCESS;
	if (ret)
		observed_wait = true;
	return ret;
}

void FenceHolderDeleter::operator()(Vulkan::FenceHolder *fence)
{
	fence->device->handle_pool.fences.free(fence);
}
}