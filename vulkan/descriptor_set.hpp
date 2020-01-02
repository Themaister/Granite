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

#include "hash.hpp"
#include "object_pool.hpp"
#include "temporary_hashmap.hpp"
#include "vulkan_headers.hpp"
#include "sampler.hpp"
#include "limits.hpp"
#include <utility>
#include <vector>
#include "cookie.hpp"

namespace Vulkan
{
class Device;
struct DescriptorSetLayout
{
	uint32_t sampled_image_mask = 0;
	uint32_t storage_image_mask = 0;
	uint32_t uniform_buffer_mask = 0;
	uint32_t storage_buffer_mask = 0;
	uint32_t sampled_buffer_mask = 0;
	uint32_t input_attachment_mask = 0;
	uint32_t sampler_mask = 0;
	uint32_t separate_image_mask = 0;
	uint32_t fp_mask = 0;
	uint32_t immutable_sampler_mask = 0;
	uint64_t immutable_samplers = 0;
	uint8_t array_size[VULKAN_NUM_BINDINGS] = {};
	enum { UNSIZED_ARRAY = 0xff };
};

// Avoid -Wclass-memaccess warnings since we hash DescriptorSetLayout.

static inline bool has_immutable_sampler(const DescriptorSetLayout &layout, unsigned binding)
{
	return (layout.immutable_sampler_mask & (1u << binding)) != 0;
}

static inline StockSampler get_immutable_sampler(const DescriptorSetLayout &layout, unsigned binding)
{
	VK_ASSERT(has_immutable_sampler(layout, binding));
	return static_cast<StockSampler>((layout.immutable_samplers >> (4 * binding)) & 0xf);
}

static inline void set_immutable_sampler(DescriptorSetLayout &layout, unsigned binding, StockSampler sampler)
{
	layout.immutable_samplers |= uint64_t(sampler) << (4 * binding);
	layout.immutable_sampler_mask |= 1u << binding;
}

static const unsigned VULKAN_NUM_SETS_PER_POOL = 16;
static const unsigned VULKAN_DESCRIPTOR_RING_SIZE = 8;

class DescriptorSetAllocator;
class BindlessDescriptorPool;
class ImageView;

struct BindlessDescriptorPoolDeleter
{
	void operator()(BindlessDescriptorPool *pool);
};

class BindlessDescriptorPool : public Util::IntrusivePtrEnabled<BindlessDescriptorPool, BindlessDescriptorPoolDeleter, HandleCounter>,
                               public InternalSyncEnabled
{
public:
	friend struct BindlessDescriptorPoolDeleter;
	explicit BindlessDescriptorPool(Device *device, DescriptorSetAllocator *allocator, VkDescriptorPool pool);
	~BindlessDescriptorPool();
	void operator=(const BindlessDescriptorPool &) = delete;
	BindlessDescriptorPool(const BindlessDescriptorPool &) = delete;

	bool allocate_descriptors(unsigned count);
	VkDescriptorSet get_descriptor_set() const;

	void set_texture(unsigned binding, const ImageView &view);
	void set_texture_unorm(unsigned binding, const ImageView &view);
	void set_texture_srgb(unsigned binding, const ImageView &view);

private:
	Device *device;
	DescriptorSetAllocator *allocator;
	VkDescriptorPool desc_pool;
	VkDescriptorSet desc_set = VK_NULL_HANDLE;

	void set_texture(unsigned binding, VkImageView view, VkImageLayout layout);
};
using BindlessDescriptorPoolHandle = Util::IntrusivePtr<BindlessDescriptorPool>;

enum class BindlessResourceType
{
	ImageFP,
	ImageInt
};

class DescriptorSetAllocator : public HashedObject<DescriptorSetAllocator>
{
public:
	DescriptorSetAllocator(Util::Hash hash, Device *device, const DescriptorSetLayout &layout, const uint32_t *stages_for_bindings);
	~DescriptorSetAllocator();
	void operator=(const DescriptorSetAllocator &) = delete;
	DescriptorSetAllocator(const DescriptorSetAllocator &) = delete;

	void begin_frame();
	std::pair<VkDescriptorSet, bool> find(unsigned thread_index, Util::Hash hash);

	VkDescriptorSetLayout get_layout() const
	{
		return set_layout;
	}

	void clear();

	bool is_bindless() const
	{
		return bindless;
	}

	VkDescriptorPool allocate_bindless_pool(unsigned num_sets, unsigned num_descriptors);
	VkDescriptorSet allocate_bindless_set(VkDescriptorPool pool, unsigned num_descriptors);

private:
	struct DescriptorSetNode : Util::TemporaryHashmapEnabled<DescriptorSetNode>, Util::IntrusiveListEnabled<DescriptorSetNode>
	{
		explicit DescriptorSetNode(VkDescriptorSet set_)
		    : set(set_)
		{
		}

		VkDescriptorSet set;
	};

	Device *device;
	const VolkDeviceTable &table;
	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;

	struct PerThread
	{
		Util::TemporaryHashmap<DescriptorSetNode, VULKAN_DESCRIPTOR_RING_SIZE, true> set_nodes;
		std::vector<VkDescriptorPool> pools;
		bool should_begin = true;
	};
	std::vector<std::unique_ptr<PerThread>> per_thread;
	std::vector<VkDescriptorPoolSize> pool_size;
	bool bindless = false;
};
}
