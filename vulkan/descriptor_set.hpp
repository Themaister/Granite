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

#include "hash.hpp"
#include "object_pool.hpp"
#include "temporary_hashmap.hpp"
#include "vulkan_headers.hpp"
#include "sampler.hpp"
#include "limits.hpp"
#include "dynamic_array.hpp"
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
	uint32_t sampled_texel_buffer_mask = 0;
	uint32_t storage_texel_buffer_mask = 0;
	uint32_t input_attachment_mask = 0;
	uint32_t sampler_mask = 0;
	uint32_t separate_image_mask = 0;
	uint32_t fp_mask = 0;
	uint32_t immutable_sampler_mask = 0;
	uint8_t array_size[VULKAN_NUM_BINDINGS] = {};
	uint32_t padding = 0;
	enum { UNSIZED_ARRAY = 0xff };
};

// Avoid -Wclass-memaccess warnings since we hash DescriptorSetLayout.

static const unsigned VULKAN_NUM_SETS_PER_POOL = 64;
static const unsigned VULKAN_DESCRIPTOR_RING_SIZE = 16;

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
	explicit BindlessDescriptorPool(Device *device, DescriptorSetAllocator *allocator, VkDescriptorPool pool,
	                                uint32_t total_sets, uint32_t total_descriptors);
	~BindlessDescriptorPool();
	void operator=(const BindlessDescriptorPool &) = delete;
	BindlessDescriptorPool(const BindlessDescriptorPool &) = delete;

	void reset();
	bool allocate_descriptors(unsigned count);
	VkDescriptorSet get_descriptor_set() const;

	void push_texture(const ImageView &view);
	void push_texture_unorm(const ImageView &view);
	void push_texture_srgb(const ImageView &view);
	void update();

private:
	Device *device;
	DescriptorSetAllocator *allocator;
	VkDescriptorPool desc_pool;
	VkDescriptorSet desc_set = VK_NULL_HANDLE;

	uint32_t allocated_sets = 0;
	uint32_t total_sets = 0;
	uint32_t allocated_descriptor_count = 0;
	uint32_t total_descriptors = 0;

	void push_texture(VkImageView view, VkImageLayout layout);
	Util::DynamicArray<VkDescriptorImageInfo> infos;
	uint32_t write_count = 0;
};
using BindlessDescriptorPoolHandle = Util::IntrusivePtr<BindlessDescriptorPool>;

enum class BindlessResourceType
{
	Image
};

class DescriptorSetAllocator : public HashedObject<DescriptorSetAllocator>
{
public:
	DescriptorSetAllocator(Util::Hash hash, Device *device, const DescriptorSetLayout &layout,
	                       const uint32_t *stages_for_bindings,
	                       const ImmutableSampler * const *immutable_samplers);
	~DescriptorSetAllocator();
	void operator=(const DescriptorSetAllocator &) = delete;
	DescriptorSetAllocator(const DescriptorSetAllocator &) = delete;

	void begin_frame();
	VkDescriptorSet request_descriptor_set(unsigned thread_index, unsigned frame_context);

	VkDescriptorSetLayout get_layout_for_pool() const
	{
		return set_layout_pool;
	}

	VkDescriptorSetLayout get_layout_for_push() const
	{
		return set_layout_push;
	}

	void clear();

	bool is_bindless() const
	{
		return bindless;
	}

	VkDescriptorPool allocate_bindless_pool(unsigned num_sets, unsigned num_descriptors);
	VkDescriptorSet allocate_bindless_set(VkDescriptorPool pool, unsigned num_descriptors);
	void reset_bindless_pool(VkDescriptorPool pool);

private:
	Device *device;
	const VolkDeviceTable &table;
	VkDescriptorSetLayout set_layout_pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout set_layout_push = VK_NULL_HANDLE;

	struct Pool
	{
		VkDescriptorPool pool;
		VkDescriptorSet sets[VULKAN_NUM_SETS_PER_POOL];
	};

	struct PerThreadAndFrame
	{
		std::vector<Pool *> pools;
		Util::ObjectPool<Pool> object_pool;
		uint32_t offset = 0;
	};

	std::vector<PerThreadAndFrame> per_thread_and_frame;
	std::vector<VkDescriptorPoolSize> pool_size;
	bool bindless = false;
};

class BindlessAllocator
{
public:
	void reserve_max_resources_per_pool(unsigned set_count, unsigned descriptor_count);
	void set_bindless_resource_type(BindlessResourceType type);

	void begin();
	unsigned push(const ImageView &view);
	VkDescriptorSet commit(Device &device);

	unsigned get_next_offset() const;

	void reset();

private:
	BindlessDescriptorPoolHandle descriptor_pool;
	unsigned max_sets_per_pool = 0;
	unsigned max_descriptors_per_pool = 0;
	BindlessResourceType resource_type = BindlessResourceType::Image;
	std::vector<const ImageView *> views;
};
}
