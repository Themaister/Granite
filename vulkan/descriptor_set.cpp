/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#define NOMINMAX
#include "descriptor_set.hpp"
#include "device.hpp"
#include <vector>

using namespace Util;

namespace Vulkan
{
DescriptorSetAllocator::DescriptorSetAllocator(Hash hash, Device *device_, const DescriptorSetLayout &layout,
                                               const uint32_t *stages_for_binds,
                                               const ImmutableSampler * const *immutable_samplers)
	: IntrusiveHashMapEnabled<DescriptorSetAllocator>(hash)
	, device(device_)
	, table(device_->get_device_table())
{
	bindless = layout.array_size[0] == DescriptorSetLayout::UNSIZED_ARRAY;

	if (!bindless)
	{
		unsigned count = device_->num_thread_indices * device_->per_frame.size();
		per_thread_and_frame.resize(count);
	}

	if (bindless && !device->get_device_features().vk12_features.descriptorIndexing)
	{
		LOGE("Cannot support descriptor indexing on this device.\n");
		return;
	}

	VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
	VkSampler vk_immutable_samplers[VULKAN_NUM_BINDINGS] = {};
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	VkDescriptorBindingFlagsEXT binding_flags = 0;

	if (bindless)
	{
		if (!device->ext.supports_descriptor_buffer)
			info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		info.pNext = &flags;

		flags.bindingCount = 1;
		flags.pBindingFlags = &binding_flags;

		binding_flags = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
		if (!device->ext.supports_descriptor_buffer)
		{
			// These flags are implied when using descriptor buffer.
			binding_flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			                 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		}
	}

	if (device->ext.supports_descriptor_buffer)
		info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

	for (unsigned i = 0; i < VULKAN_NUM_BINDINGS; i++)
	{
		auto stages = stages_for_binds[i];
		if (stages == 0)
			continue;

		unsigned array_size = layout.array_size[i];
		unsigned pool_array_size;
		if (array_size == DescriptorSetLayout::UNSIZED_ARRAY)
		{
			array_size = VULKAN_NUM_BINDINGS_BINDLESS_VARYING;
			pool_array_size = array_size;
		}
		else
			pool_array_size = array_size * VULKAN_NUM_SETS_PER_POOL;

		unsigned types = 0;
		if (layout.sampled_image_mask & (1u << i))
		{
			if ((layout.immutable_sampler_mask & (1u << i)) && immutable_samplers && immutable_samplers[i])
				vk_immutable_samplers[i] = immutable_samplers[i]->get_sampler().get_sampler();

			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, array_size, stages,
			                     vk_immutable_samplers[i] != VK_NULL_HANDLE ? &vk_immutable_samplers[i] : nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, pool_array_size });
			types++;
		}

		if (layout.sampled_texel_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, pool_array_size });
			types++;
		}

		if (layout.storage_texel_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, pool_array_size });
			types++;
		}

		if (layout.storage_image_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, pool_array_size });
			types++;
		}

		if (layout.uniform_buffer_mask & (1u << i))
		{
			auto type = device->get_device_features().supports_descriptor_buffer ?
			            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			bindings.push_back({ i, type, array_size, stages, nullptr });
			pool_size.push_back({ type, pool_array_size });
			types++;
		}

		if (layout.storage_buffer_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, pool_array_size });
			types++;
		}

		if (layout.input_attachment_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, pool_array_size });
			types++;
		}

		if (layout.separate_image_mask & (1u << i))
		{
			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, array_size, stages, nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, pool_array_size });
			types++;
		}

		if (layout.sampler_mask & (1u << i))
		{
			if ((layout.immutable_sampler_mask & (1u << i)) && immutable_samplers && immutable_samplers[i])
			{
				if (!device->get_device_features().supports_descriptor_buffer)
					vk_immutable_samplers[i] = immutable_samplers[i]->get_sampler().get_sampler();
				else
					LOGE("Cannot use immutable samplers with descriptor buffer. Ignoring.\n");
			}

			bindings.push_back({ i, VK_DESCRIPTOR_TYPE_SAMPLER, array_size, stages,
			                     vk_immutable_samplers[i] != VK_NULL_HANDLE ? &vk_immutable_samplers[i] : nullptr });
			pool_size.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, pool_array_size });
			types++;
		}

		(void)types;
		VK_ASSERT(types <= 1 && "Descriptor set aliasing!");
	}

	if (!bindings.empty())
	{
		info.bindingCount = bindings.size();
		info.pBindings = bindings.data();

		if (bindless && bindings.size() != 1)
		{
			LOGE("Using bindless but have bindingCount != 1.\n");
			return;
		}
	}

#ifdef VULKAN_DEBUG
	LOGI("Creating descriptor set layout.\n");
#endif
	if (table.vkCreateDescriptorSetLayout(device->get_device(), &info, nullptr, &set_layout_pool) != VK_SUCCESS)
		LOGE("Failed to create descriptor set layout.");

	if (device->ext.supports_descriptor_buffer)
	{
		// Query the memory layout.
		table.vkGetDescriptorSetLayoutSizeEXT(device->get_device(), set_layout_pool, &desc_set_size);

		if (bindless)
		{
			table.vkGetDescriptorSetLayoutBindingOffsetEXT(
					device->get_device(), set_layout_pool, 0, &desc_set_variable_offset);
		}
		else
		{
			for (auto &bind : bindings)
			{
				VkDeviceSize offset = 0;
				VkDeviceSize stride = device->managers.descriptor_buffer.get_descriptor_size_for_type(bind.descriptorType);

				table.vkGetDescriptorSetLayoutBindingOffsetEXT(
						device->get_device(), set_layout_pool, bind.binding, &offset);

				for (uint32_t i = 0; i < bind.descriptorCount; i++)
					desc_offsets[bind.binding + i] = offset + i * stride;
			}
		}
	}

#ifdef GRANITE_VULKAN_FOSSILIZE
	if (device->ext.supports_descriptor_buffer)
	{
		// Normalize the recorded flags.
		if (bindless)
		{
			info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
			binding_flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			                 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		}

		info.flags &= ~VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	}

	if (set_layout_pool)
		device->register_descriptor_set_layout(set_layout_pool, get_hash(), info);
#endif

	// Push descriptors is not used with descriptor buffer.
	if (!bindless && device->get_device_features().vk14_features.pushDescriptor &&
	    !device->get_device_features().descriptor_buffer_features.descriptorBuffer)
	{
		info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
		for (auto &b : bindings)
			if (b.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
				b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		if (table.vkCreateDescriptorSetLayout(device->get_device(), &info, nullptr, &set_layout_push) != VK_SUCCESS)
			LOGE("Failed to create descriptor set layout.");
#ifdef GRANITE_VULKAN_FOSSILIZE
		if (set_layout_push)
			device->register_descriptor_set_layout(set_layout_push, get_hash(), info);
#endif
	}
}

void DescriptorSetAllocator::reset_bindless_pool(VkDescriptorPool pool)
{
	table.vkResetDescriptorPool(device->get_device(), pool, 0);
}

BindlessDescriptorSet DescriptorSetAllocator::allocate_bindless_set(VkDescriptorPool pool, unsigned num_descriptors)
{
	if (!pool || !bindless)
		return {};

	VkDescriptorSetAllocateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	info.descriptorPool = pool;
	info.descriptorSetCount = 1;
	info.pSetLayouts = &set_layout_pool;

	VkDescriptorSetVariableDescriptorCountAllocateInfoEXT count_info =
			{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT };

	uint32_t num_desc = num_descriptors;
	count_info.descriptorSetCount = 1;
	count_info.pDescriptorCounts = &num_desc;
	info.pNext = &count_info;

	BindlessDescriptorSet desc_set;
	if (table.vkAllocateDescriptorSets(device->get_device(), &info, &desc_set.handle.set) != VK_SUCCESS)
		return {};

	desc_set.valid = true;
	return desc_set;
}

VkDescriptorPool DescriptorSetAllocator::allocate_bindless_pool(unsigned num_sets, unsigned num_descriptors)
{
	if (!bindless)
		return VK_NULL_HANDLE;

	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
	info.maxSets = num_sets;
	info.poolSizeCount = 1;

	VkDescriptorPoolSize size = pool_size[0];
	size.descriptorCount = num_descriptors;
	info.pPoolSizes = &size;

	if (table.vkCreateDescriptorPool(device->get_device(), &info, nullptr, &pool) != VK_SUCCESS)
	{
		LOGE("Failed to create descriptor pool.\n");
		return VK_NULL_HANDLE;
	}

	return pool;
}

void DescriptorSetAllocator::begin_frame()
{
	if (!bindless)
	{
		// This can only be called in a situation where no command buffers are alive,
		// so we don't need to consider any locks here.
		if (device->per_frame.size() * device->num_thread_indices != per_thread_and_frame.size())
			per_thread_and_frame.resize(device->per_frame.size() * device->num_thread_indices);

		// It would be safe to set all offsets to 0 here, but that's a little wasteful.
		for (uint32_t i = 0; i < device->num_thread_indices; i++)
			per_thread_and_frame[i * device->per_frame.size() + device->frame_context_index].offset = 0;
	}
}

VkDescriptorSet DescriptorSetAllocator::request_descriptor_set(unsigned thread_index, unsigned frame_index)
{
	VK_ASSERT(!bindless);

	size_t flattened_index = thread_index * device->per_frame.size() + frame_index;

	auto &state = per_thread_and_frame[flattened_index];

	unsigned pool_index = state.offset / VULKAN_NUM_SETS_PER_POOL;
	unsigned pool_offset = state.offset % VULKAN_NUM_SETS_PER_POOL;

	if (pool_index >= state.pools.size())
	{
		Pool *pool = state.object_pool.allocate();

		VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		info.maxSets = VULKAN_NUM_SETS_PER_POOL;
		if (!pool_size.empty())
		{
			info.poolSizeCount = pool_size.size();
			info.pPoolSizes = pool_size.data();
		}

		bool overallocation =
		    device->get_device_features().descriptor_pool_overallocation_features.descriptorPoolOverallocation ==
		    VK_TRUE;

		if (overallocation)
		{
			// No point in allocating new pools if we can keep using the existing one.
			info.flags |= VK_DESCRIPTOR_POOL_CREATE_ALLOW_OVERALLOCATION_POOLS_BIT_NV |
			              VK_DESCRIPTOR_POOL_CREATE_ALLOW_OVERALLOCATION_SETS_BIT_NV;
		}

		bool need_alloc = !overallocation || state.pools.empty();

		pool->pool = VK_NULL_HANDLE;
		if (need_alloc && table.vkCreateDescriptorPool(device->get_device(), &info, nullptr, &pool->pool) != VK_SUCCESS)
		{
			LOGE("Failed to create descriptor pool.\n");
			state.object_pool.free(pool);
			return VK_NULL_HANDLE;
		}

		VkDescriptorSetLayout layouts[VULKAN_NUM_SETS_PER_POOL];
		std::fill(std::begin(layouts), std::end(layouts), set_layout_pool);

		VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		alloc.descriptorPool = pool->pool != VK_NULL_HANDLE ? pool->pool : state.pools.front()->pool;
		alloc.descriptorSetCount = VULKAN_NUM_SETS_PER_POOL;
		alloc.pSetLayouts = layouts;

		if (table.vkAllocateDescriptorSets(device->get_device(), &alloc, pool->sets) != VK_SUCCESS)
			LOGE("Failed to allocate descriptor sets.\n");
		state.pools.push_back(pool);
	}

	VkDescriptorSet vk_set = state.pools[pool_index]->sets[pool_offset];
	state.offset++;
	return vk_set;
}

void DescriptorSetAllocator::clear()
{
	for (auto &state : per_thread_and_frame)
	{
		for (auto *obj : state.pools)
		{
			table.vkDestroyDescriptorPool(device->get_device(), obj->pool, nullptr);
			state.object_pool.free(obj);
		}
		state.pools.clear();
		state.offset = 0;
		state.object_pool = {};
	}
}

DescriptorSetAllocator::~DescriptorSetAllocator()
{
	table.vkDestroyDescriptorSetLayout(device->get_device(), set_layout_pool, nullptr);
	table.vkDestroyDescriptorSetLayout(device->get_device(), set_layout_push, nullptr);
	clear();
}

BindlessDescriptorPool::BindlessDescriptorPool(Device *device_, DescriptorSetAllocator *allocator_,
                                               VkDescriptorPool pool, uint32_t num_sets, uint32_t num_desc)
	: device(device_), allocator(allocator_), desc_pool(pool), total_sets(num_sets), total_descriptors(num_desc)
{
}

BindlessDescriptorPool::~BindlessDescriptorPool()
{
	if (desc_pool)
	{
		if (internal_sync)
			device->destroy_descriptor_pool_nolock(desc_pool);
		else
			device->destroy_descriptor_pool(desc_pool);
	}
}

BindlessDescriptorSet BindlessDescriptorPool::get_descriptor_set() const
{
	return desc_set;
}

void BindlessDescriptorPool::reset()
{
	if (desc_pool != VK_NULL_HANDLE)
		allocator->reset_bindless_pool(desc_pool);
	desc_set = {};
	allocated_descriptor_count = 0;
	allocated_sets = 0;
}

bool BindlessDescriptorPool::allocate_descriptors(unsigned count)
{
	// Not all drivers will exhaust the pool for us, so make sure we don't allocate more than expected.
	if (allocated_sets == total_sets)
		return false;
	if (allocated_descriptor_count + count > total_descriptors)
		return false;

	allocated_descriptor_count += count;
	allocated_sets++;

	desc_set = allocator->allocate_bindless_set(desc_pool, count);

	infos.reserve(count);
	write_count = 0;

	return bool(desc_set);
}

void BindlessDescriptorPool::push_texture(const ImageView &view)
{
	// TODO: Deal with integer view for depth-stencil images?
	push_texture(view.get_float_view().view, view.get_image().get_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

void BindlessDescriptorPool::push_texture_unorm(const ImageView &view)
{
	push_texture(view.get_unorm_view().view, view.get_image().get_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

void BindlessDescriptorPool::push_texture_srgb(const ImageView &view)
{
	push_texture(view.get_srgb_view().view, view.get_image().get_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

void BindlessDescriptorPool::push_texture(VkImageView view, VkImageLayout layout)
{
	VK_ASSERT(write_count < infos.get_capacity());
	auto &image_info = infos[write_count];
	image_info = { VK_NULL_HANDLE, view, layout };
	write_count++;
}

void BindlessDescriptorPool::update()
{
	VkWriteDescriptorSet desc = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	desc.descriptorCount = write_count;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	desc.dstSet = desc_set.handle.set;

	desc.pImageInfo = infos.data();
	desc.pBufferInfo = nullptr;
	desc.pTexelBufferView = nullptr;

	if (write_count)
	{
		auto &table = device->get_device_table();
		table.vkUpdateDescriptorSets(device->get_device(), 1, &desc, 0, nullptr);
	}
}

void BindlessDescriptorPoolDeleter::operator()(BindlessDescriptorPool *pool)
{
	pool->device->handle_pool.bindless_descriptor_pool.free(pool);
}

unsigned BindlessAllocator::push(const ImageView &view)
{
	auto ret = unsigned(views.size());
	views.push_back(&view);
	if (views.size() > VULKAN_NUM_BINDINGS_BINDLESS_VARYING)
	{
		LOGE("Exceeding maximum number of bindless resources per set (%u >= %u).\n",
		     unsigned(views.size()), VULKAN_NUM_BINDINGS_BINDLESS_VARYING);
	}
	return ret;
}

void BindlessAllocator::begin()
{
	views.clear();
}

void BindlessAllocator::reset()
{
	descriptor_pool.reset();
}

unsigned BindlessAllocator::get_next_offset() const
{
	return unsigned(views.size());
}

void BindlessAllocator::reserve_max_resources_per_pool(unsigned set_count, unsigned descriptor_count)
{
	max_sets_per_pool = std::max(max_sets_per_pool, set_count);
	max_descriptors_per_pool = std::max(max_descriptors_per_pool, descriptor_count);
	views.reserve(max_descriptors_per_pool);
}

void BindlessAllocator::set_bindless_resource_type(BindlessResourceType type)
{
	resource_type = type;
}

BindlessDescriptorSet BindlessAllocator::commit(Device &device)
{
	max_sets_per_pool = std::max(1u, max_sets_per_pool);
	max_descriptors_per_pool = std::max<unsigned>(views.size(), max_descriptors_per_pool);
	max_descriptors_per_pool = std::max<unsigned>(1u, max_descriptors_per_pool);
	max_descriptors_per_pool = std::min(max_descriptors_per_pool, VULKAN_NUM_BINDINGS_BINDLESS_VARYING);
	unsigned to_allocate = std::max<unsigned>(views.size(), 1u);

	if (!descriptor_pool)
	{
		descriptor_pool = device.create_bindless_descriptor_pool(
				resource_type, max_sets_per_pool, max_descriptors_per_pool);
	}

	if (!descriptor_pool->allocate_descriptors(to_allocate))
	{
		descriptor_pool = device.create_bindless_descriptor_pool(
				resource_type, max_sets_per_pool, max_descriptors_per_pool);

		if (!descriptor_pool->allocate_descriptors(to_allocate))
		{
			LOGE("Failed to allocate descriptors on a fresh descriptor pool!\n");
			return {};
		}
	}

	for (size_t i = 0, n = views.size(); i < n; i++)
		descriptor_pool->push_texture(*views[i]);
	descriptor_pool->update();

	return descriptor_pool->get_descriptor_set();
}
}
