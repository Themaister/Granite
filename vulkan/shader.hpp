/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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

#include "cookie.hpp"
#include "descriptor_set.hpp"
#include "hash.hpp"
#include "intrusive.hpp"
#include "limits.hpp"
#include "vulkan_headers.hpp"
#include "enum_cast.hpp"

namespace spirv_cross
{
struct SPIRType;
}

namespace Vulkan
{
class Device;

enum class ShaderStage
{
	Vertex = 0,
	Fragment = 4, // Skip over tess/geom to match Vulkan ordering.
	Compute,
	Task,
	Mesh,
	Count
};

struct ResourceLayout
{
	DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS];
	uint32_t input_mask = 0;
	uint32_t output_mask = 0;
	uint32_t push_constant_size = 0;
	uint32_t spec_constant_mask = 0;
	uint32_t bindless_set_mask = 0;
	enum { Version = 7 };

	bool unserialize(const uint8_t *data, size_t size);
	bool serialize(uint8_t *data, size_t size) const;
	static size_t serialization_size();
};
static_assert(sizeof(DescriptorSetLayout) % 8 == 0, "Size of DescriptorSetLayout does not align to 64 bits.");

struct CombinedResourceLayout
{
	uint32_t attribute_mask = 0;
	uint32_t render_target_mask = 0;
	DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	uint32_t stages_for_bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
	uint32_t stages_for_sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	VkPushConstantRange push_constant_range = {};
	uint32_t descriptor_set_mask = 0;
	uint32_t bindless_descriptor_set_mask = 0;
	uint32_t spec_constant_mask[Util::ecast(ShaderStage::Count)] = {};
	uint32_t combined_spec_constant_mask = 0;
	Util::Hash push_constant_layout_hash = 0;
};

union CombinedImageSamplerIndex
{
	struct
	{
		uint32_t image_heap_index : 20;
		uint32_t sampler_heap_index : 12;
	};
	uint32_t word;
};
static_assert(sizeof(CombinedImageSamplerIndex) == sizeof(uint32_t), "Unexpected size of CombinedImageSamplerIndex.");

union ResourceBinding
{
	VkDescriptorBufferInfo buffer;

	struct
	{
		VkDescriptorImageInfo fp;
		VkDescriptorImageInfo integer;
		const uint8_t *fp_ptr;
		const uint8_t *integer_ptr;
		const uint8_t *sampler_ptr;
		CombinedImageSamplerIndex fp_heap_index;
		CombinedImageSamplerIndex integer_heap_index;
	} image;

	VkDescriptorAddressInfoEXT buffer_addr_buffer;
	VkDeviceAddressRangeEXT buffer_addr_heap;
	VkAccelerationStructureKHR rtas;

	union
	{
		VkBufferView handle;
		struct
		{
			const uint8_t *ptr;
			uint32_t heap_index;
		} buffer;
	} buffer_view;
};

struct ResourceBindings
{
	ResourceBinding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint64_t cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint64_t secondary_cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint8_t push_constant_data[VULKAN_PUSH_CONSTANT_SIZE];

	union
	{
		uint32_t push_data_words[(VULKAN_PUSH_DATA_SIZE - VULKAN_PUSH_CONSTANT_SIZE) / (VULKAN_NUM_DESCRIPTOR_SETS * sizeof(uint32_t))];
		VkDeviceAddress push_data_addr[(VULKAN_PUSH_DATA_SIZE - VULKAN_PUSH_CONSTANT_SIZE) / (VULKAN_NUM_DESCRIPTOR_SETS * sizeof(VkDeviceAddress))];
	} inline_descriptors[VULKAN_NUM_DESCRIPTOR_SETS];
};

struct ImmutableSamplerBank
{
	const ImmutableSampler *samplers[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	static void hash(Util::Hasher &h, const ImmutableSamplerBank *bank);
};

class PipelineLayout : public HashedObject<PipelineLayout>
{
public:
	PipelineLayout(Util::Hash hash, Device *device, const CombinedResourceLayout &layout,
				   const ImmutableSamplerBank *sampler_bank);
	~PipelineLayout();

	const CombinedResourceLayout &get_resource_layout() const
	{
		return layout;
	}

	// Legacy
	VkPipelineLayout get_layout() const
	{
		return pipe_layout;
	}

	DescriptorSetAllocator *get_allocator(unsigned set) const
	{
		return set_allocators[set];
	}

	VkDescriptorUpdateTemplate get_update_template(unsigned set) const
	{
		return update_template[set];
	}

	uint32_t get_push_set_index() const
	{
		return push_set_index;
	}

	// Heap
	enum class DescriptorStrategy
	{
		// For images: a u32 index. For buffers: PUSH_ADDRESS.
		Inline,
		// Not compatible with array of samplers or combined image samplers.
		// Not compatible with SSBO that need ArrayLength.
		HeapSlice,
		// Indirect version of inline, for larger sets.
		IndirectTable,
	};

	// Allocation size from indirection table UBO.
	uint32_t get_heap_table_size(uint32_t desc_set) const
	{
		return heap.heap_table_size[desc_set];
	}

	// Allocation size from descriptor heap.
	// Used when we want to copy descriptors straight into the heap.
	uint32_t get_heap_slice_size(uint32_t desc_set) const
	{
		return heap.heap_slice_size[desc_set];
	}

	uint32_t get_descriptor_set_push_buffer_offset(uint32_t desc_set) const
	{
		VK_ASSERT(desc_set < VULKAN_NUM_DESCRIPTOR_SETS);
		return heap.push_buffer_offsets[desc_set];
	}

	uint32_t get_descriptor_set_push_image_offset(uint32_t desc_set) const
	{
		VK_ASSERT(desc_set < VULKAN_NUM_DESCRIPTOR_SETS);
		return heap.push_image_offsets[desc_set];
	}

	uint32_t get_descriptor_set_inline_offsets(uint32_t desc_set) const
	{
		VK_ASSERT(desc_set < VULKAN_NUM_DESCRIPTOR_SETS);
		return heap.push_inline_offsets[desc_set];
	}

	uint32_t get_descriptor_set_inline_size(uint32_t desc_set) const
	{
		VK_ASSERT(desc_set < VULKAN_NUM_DESCRIPTOR_SETS);
		return heap.push_inline_size[desc_set];
	}

	DescriptorStrategy get_heap_buffer_descriptor_strategy(uint32_t desc_set) const
	{
		VK_ASSERT(desc_set < VULKAN_NUM_DESCRIPTOR_SETS);
		return heap.buffer_strategies[desc_set];
	}

	DescriptorStrategy get_heap_image_descriptor_strategy(uint32_t desc_set) const
	{
		VK_ASSERT(desc_set < VULKAN_NUM_DESCRIPTOR_SETS);
		return heap.image_strategies[desc_set];
	}

	// Inline: local offset into inline push data
	// HeapSlice: offset into allocated heap slice
	// IndirectTable: offset into indirect table
	uint32_t get_descriptor_offset(uint32_t desc_set, uint32_t binding) const
	{
		VK_ASSERT(desc_set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		return heap.desc_offsets[desc_set][binding];
	}

	// Passed directly to CreatePipeline.
	const std::vector<VkDescriptorSetAndBindingMappingEXT> &get_heap_mappings() const
	{
		return heap.mappings;
	}

private:
	Device *device;
	VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
	CombinedResourceLayout layout;
	DescriptorSetAllocator *set_allocators[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	VkDescriptorUpdateTemplate update_template[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	uint32_t push_set_index = UINT32_MAX;
	void create_update_templates();

	void init_heap();
	void init_heap(uint32_t set_index);
	void init_heap_buffers(uint32_t set_index);
	void init_heap_image(uint32_t set_index);
	void init_heap_offsets(uint32_t set_index);
	void init_legacy(const ImmutableSamplerBank *immutable_samplers);

	struct
	{
		std::vector<VkDescriptorSetAndBindingMappingEXT> mappings;
		// Inline descriptors are packed together.
		uint32_t push_inline_offsets[VULKAN_NUM_DESCRIPTOR_SETS];
		uint32_t push_inline_size[VULKAN_NUM_DESCRIPTOR_SETS];
		// For tables and slices.
		uint32_t push_buffer_offsets[VULKAN_NUM_DESCRIPTOR_SETS];
		uint32_t push_image_offsets[VULKAN_NUM_DESCRIPTOR_SETS];
		uint32_t heap_table_size[VULKAN_NUM_DESCRIPTOR_SETS];
		uint32_t heap_slice_size[VULKAN_NUM_DESCRIPTOR_SETS];
		DescriptorStrategy buffer_strategies[VULKAN_NUM_DESCRIPTOR_SETS];
		DescriptorStrategy image_strategies[VULKAN_NUM_DESCRIPTOR_SETS];
		uint32_t desc_offsets[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
		uint32_t push_data_size;
	} heap = {};
};

class Shader : public HashedObject<Shader>
{
public:
	Shader(Util::Hash binding, Device *device, const uint32_t *data, size_t size,
	       const ResourceLayout *layout = nullptr);
	~Shader();

	const ResourceLayout &get_layout() const
	{
		return layout;
	}

	VkShaderModule get_module() const
	{
		return module;
	}

	static bool reflect_resource_layout(ResourceLayout &layout, const uint32_t *spirv_data, size_t spirv_size);
	static const char *stage_to_name(ShaderStage stage);
	static Util::Hash hash(const uint32_t *data, size_t size);

private:
	Device *device;
	VkShaderModule module = VK_NULL_HANDLE;
	ResourceLayout layout;
};

struct Pipeline
{
	VkPipeline pipeline;
	uint32_t dynamic_mask;
};

class Program : public HashedObject<Program>
{
public:
	Program(Device *device, Shader *vertex, Shader *fragment, const ImmutableSamplerBank *sampler_bank);
	Program(Device *device, Shader *task, Shader *mesh, Shader *fragment, const ImmutableSamplerBank *sampler_bank);
	Program(Device *device, Shader *compute, const ImmutableSamplerBank *sampler_bank);
	~Program();

	inline const Shader *get_shader(ShaderStage stage) const
	{
		return shaders[Util::ecast(stage)];
	}

	void set_pipeline_layout(const PipelineLayout *new_layout)
	{
		layout = new_layout;
	}

	const PipelineLayout *get_pipeline_layout() const
	{
		return layout;
	}

	Pipeline get_pipeline(Util::Hash hash) const;
	Pipeline add_pipeline(Util::Hash hash, const Pipeline &pipeline);

	void promote_read_write_to_read_only();

private:
	void set_shader(ShaderStage stage, Shader *handle);
	Device *device;
	Shader *shaders[Util::ecast(ShaderStage::Count)] = {};
	const PipelineLayout *layout = nullptr;
	VulkanCache<Util::IntrusivePODWrapper<Pipeline>> pipelines;
	void destroy_pipeline(const Pipeline &pipeline);
};
}
