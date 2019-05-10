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

#include "descriptor_set.hpp"
#include "device.hpp"
#include <vector>

using namespace std;
using namespace Util;

namespace Vulkan
{
DescriptorSetAllocator::DescriptorSetAllocator(Hash hash, Device *device_, const DescriptorSetLayout &layout, const uint32_t *stages_for_binds)
	: IntrusiveHashMapEnabled<DescriptorSetAllocator>(hash)
	, device(device_)
	, table(device_->get_device_table())
{
	unsigned count = device_->num_thread_indices;
	for (unsigned i = 0; i < count; i++)
		per_thread.emplace_back(new PerThread);

	VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

	vector<VkDescriptorSetLayoutBinding> bindings;
	for (unsigned i = 0; i < VULKAN_NUM_BINDINGS; i++)
	{
		auto stages = stages_for_binds[i];
		if (stages == 0)
			continue;

		unsigned array_size = layout.array_size[i];

		unsigned types = 0;
		if (layout.sampled_image_mask & (1u << i))
		{
			VkSampler sampler = VK_NULL_HANDLE;
			if (has_immutable_sampler(layout, i))
				sampler = device->get_stock_sampler(get_immutable_sampler(layout, i)).get_sampler();

			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, array_size, stages, sampler != VK_NULL_HANDLE ? &sampler : nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, array_size * VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.sampled_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, array_size * VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.storage_image_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.uniform_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, array_size * VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.storage_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, array_size * VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.input_attachment_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, array_size * VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.separate_image_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, array_size * VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		if (layout.sampler_mask & (1u << i))
		{
			VkSampler sampler = VK_NULL_HANDLE;
			if (has_immutable_sampler(layout, i))
				sampler = device->get_stock_sampler(get_immutable_sampler(layout, i)).get_sampler();

			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_SAMPLER, array_size, stages, sampler != VK_NULL_HANDLE ? &sampler : nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, array_size * VULKAN_NUM_SETS_PER_POOL });
			types++;
		}

		(void)types;
		VK_ASSERT(types <= 1 && "Descriptor set aliasing!");
	}

	if (!bindings.empty())
	{
		info.bindingCount = bindings.size();
		info.pBindings = bindings.data();
	}

	LOGI("Creating descriptor set layout.\n");
	if (table.vkCreateDescriptorSetLayout(device->get_device(), &info, nullptr, &set_layout) != VK_SUCCESS)
		LOGE("Failed to create descriptor set layout.");
#ifdef GRANITE_VULKAN_FOSSILIZE
	device->register_descriptor_set_layout(set_layout, get_hash(), info);
#endif
}

void DescriptorSetAllocator::begin_frame()
{
	for (auto &thr : per_thread)
		thr->should_begin = true;
}

pair<VkDescriptorSet, bool> DescriptorSetAllocator::find(unsigned thread_index, Hash hash)
{
	auto &state = *per_thread[thread_index];
	if (state.should_begin)
	{
		state.set_nodes.begin_frame();
		state.should_begin = false;
	}

	auto *node = state.set_nodes.request(hash);
	if (node)
		return { node->set, true };

	node = state.set_nodes.request_vacant(hash);
	if (node)
		return { node->set, false };

	VkDescriptorPool pool;
	VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	info.maxSets = VULKAN_NUM_SETS_PER_POOL;
	if (!pool_size.empty())
	{
		info.poolSizeCount = pool_size.size();
		info.pPoolSizes = pool_size.data();
	}

	if (table.vkCreateDescriptorPool(device->get_device(), &info, nullptr, &pool) != VK_SUCCESS)
		LOGE("Failed to create descriptor pool.\n");

	VkDescriptorSet sets[VULKAN_NUM_SETS_PER_POOL];
	VkDescriptorSetLayout layouts[VULKAN_NUM_SETS_PER_POOL];
	fill(begin(layouts), end(layouts), set_layout);

	VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	alloc.descriptorPool = pool;
	alloc.descriptorSetCount = VULKAN_NUM_SETS_PER_POOL;
	alloc.pSetLayouts = layouts;

	if (table.vkAllocateDescriptorSets(device->get_device(), &alloc, sets) != VK_SUCCESS)
		LOGE("Failed to allocate descriptor sets.\n");
	state.pools.push_back(pool);

	for (auto set : sets)
		state.set_nodes.make_vacant(set);

	return { state.set_nodes.request_vacant(hash)->set, false };
}

void DescriptorSetAllocator::clear()
{
	for (auto &thr : per_thread)
	{
		thr->set_nodes.clear();
		for (auto &pool : thr->pools)
		{
			table.vkResetDescriptorPool(device->get_device(), pool, 0);
			table.vkDestroyDescriptorPool(device->get_device(), pool, nullptr);
		}
		thr->pools.clear();
	}
}

DescriptorSetAllocator::~DescriptorSetAllocator()
{
	if (set_layout != VK_NULL_HANDLE)
		table.vkDestroyDescriptorSetLayout(device->get_device(), set_layout, nullptr);
	clear();
}
}
