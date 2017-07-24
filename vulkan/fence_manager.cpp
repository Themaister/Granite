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

#include "fence_manager.hpp"

namespace Vulkan
{
FenceManager::FenceManager(VkDevice device)
    : device(device)
{
}

VkFence FenceManager::request_cleared_fence()
{
	if (index < fences.size())
	{
		return fences[index++];
	}
	else
	{
		VkFence fence;
		VkFenceCreateInfo info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		vkCreateFence(device, &info, nullptr, &fence);
		fences.push_back(fence);
		index++;
		return fence;
	}
}

void FenceManager::begin()
{
	if (index)
	{
		vkWaitForFences(device, index, fences.data(), true, UINT64_MAX);
		vkResetFences(device, index, fences.data());
	}
	index = 0;
}

FenceManager::~FenceManager()
{
	if (index)
	{
		vkWaitForFences(device, index, fences.data(), true, UINT64_MAX);
		vkResetFences(device, index, fences.data());
	}

	for (auto &fence : fences)
		vkDestroyFence(device, fence, nullptr);
}
}
