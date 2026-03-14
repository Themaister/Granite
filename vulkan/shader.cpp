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

#define NOMINMAX
#include "shader.hpp"
#include "device.hpp"
#ifdef GRANITE_VULKAN_SPIRV_CROSS
#include "spirv_cross.hpp"
using namespace spirv_cross;
#endif

#ifdef HAVE_GRANITE_VULKAN_POST_MORTEM
#include "post_mortem.hpp"
#endif

using namespace Util;

namespace Vulkan
{
void ImmutableSamplerBank::hash(Util::Hasher &h, const ImmutableSamplerBank *sampler_bank)
{
	h.u32(0);
	if (sampler_bank)
	{
		unsigned index = 0;
		for (auto &set : sampler_bank->samplers)
		{
			for (auto *binding : set)
			{
				if (binding)
				{
					h.u32(index);
					h.u64(binding->get_hash());
				}
				index++;
			}
		}
	}
}

// 32 bytes is a decent amount in most cases. For cases with lots of resources, just fallback to heap slice.
static constexpr uint32_t MaxInlineSizePerSet = (256 - VULKAN_PUSH_CONSTANT_SIZE) / VULKAN_NUM_DESCRIPTOR_SETS;
// Worst case we can always fall back to heap slice or indirect table for images. This requires at most one BDA.
static constexpr uint32_t MaxBufferInlineSizePerSet = MaxInlineSizePerSet - sizeof(VkDeviceAddress);

static uint32_t align(uint32_t size, uint32_t alignment)
{
	return (size + alignment - 1) & ~(alignment - 1);
}

void PipelineLayout::init_heap()
{
	uint32_t push_data_offset = layout.push_constant_range.offset + layout.push_constant_range.size;

	auto buffer_desc_size =
			align(device->get_device_features().descriptor_heap_properties.bufferDescriptorSize,
			      device->get_device_features().descriptor_heap_properties.bufferDescriptorAlignment);

	auto image_desc_size =
			align(device->get_device_features().descriptor_heap_properties.imageDescriptorSize,
				  device->get_device_features().descriptor_heap_properties.imageDescriptorAlignment);

	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		if ((layout.descriptor_set_mask & (1u << i)) == 0)
			continue;

		auto &desc_set = layout.sets[i];
		uint32_t base_push_data_offset = push_data_offset;

		auto raw_buffer_mask = desc_set.uniform_buffer_mask | desc_set.storage_buffer_mask | desc_set.rtas_mask;
		auto num_buffer_descriptors = Util::popcount32(raw_buffer_mask);
		auto required_inline_size = num_buffer_descriptors * sizeof(VkDeviceAddress);

		bool requires_array_length = false;
		Util::for_each_bit(raw_buffer_mask, [&](unsigned bit)
		{
			if (desc_set.meta[bit].requires_descriptor_size)
				requires_array_length = true;
		});

		// Raw PUSH_ADDRESS is always preferred.
		if (required_inline_size <= MaxBufferInlineSizePerSet && !requires_array_length)
		{
			heap.buffer_strategies[i] = DescriptorStrategy::Inline;
			push_data_offset = align(push_data_offset, sizeof(VkDeviceAddress));
			heap.push_buffer_offsets[i] = push_data_offset;
			push_data_offset += required_inline_size;
		}
		else if (requires_array_length)
		{
			heap.buffer_strategies[i] = DescriptorStrategy::HeapSlice;
			heap.push_buffer_offsets[i] = push_data_offset;
			// A single u32 will do.
			push_data_offset += sizeof(uint32_t);

			// Allocate N descriptors from the heap and write them directly.
			heap.heap_slice_size[i] = align(heap.heap_slice_size[i], buffer_desc_size);
			heap.heap_slice_size[i] = num_buffer_descriptors * buffer_desc_size;
		}
		else
		{
			// Small buffer of BDAs. Don't want to allocate from the precious heap if possible.
			heap.buffer_strategies[i] = DescriptorStrategy::IndirectTable;
			push_data_offset = align(push_data_offset, sizeof(VkDeviceAddress));
			heap.push_buffer_offsets[i] = push_data_offset;
			push_data_offset += sizeof(VkDeviceAddress);

			heap.heap_table_size[i] += required_inline_size;
		}

		auto image_sampler_mask =
			desc_set.sampled_image_mask |
			desc_set.separate_image_mask | desc_set.storage_image_mask |
			desc_set.sampled_texel_buffer_mask | desc_set.storage_texel_buffer_mask |
			desc_set.sampler_mask;

		auto sampler_mask = desc_set.sampled_image_mask | desc_set.sampler_mask;
		uint32_t num_image_descriptors = Util::popcount32(image_sampler_mask);
		bool requires_array_of_image = false;

		Util::for_each_bit(image_sampler_mask, [&](unsigned bit)
		{
			if (desc_set.meta[bit].array_size > 1)
				requires_array_of_image = true;
		});

		uint32_t available_inline_indices =
			(MaxInlineSizePerSet - (push_data_offset - base_push_data_offset)) / sizeof(uint32_t);

		// Array of resources would need either heap slice or indirection table.
		if (num_image_descriptors <= available_inline_indices && !requires_array_of_image)
		{
			heap.push_image_offsets[i] = push_data_offset;
			push_data_offset += num_image_descriptors * sizeof(uint32_t);
			heap.image_strategies[i] = DescriptorStrategy::Inline;
		}
		else if (sampler_mask != 0)
		{
			// We cannot lower sampler to heap slice since sampler heap is so tiny.
			// Force indirection table.
			// TODO: It's in theory possible to split this up
			// so that samplers are push index inlined while everything else is heap sliced.

			// This isn't ideal, but what can you do.
			heap.image_strategies[i] = DescriptorStrategy::IndirectTable;

			// Buffers and images can share the same indirection table.
			if (heap.buffer_strategies[i] == DescriptorStrategy::IndirectTable)
			{
				heap.push_image_offsets[i] = heap.push_buffer_offsets[i];
			}
			else
			{
				push_data_offset = align(push_data_offset, sizeof(VkDeviceAddress));
				heap.push_image_offsets[i] = push_data_offset;
				push_data_offset += sizeof(VkDeviceAddress);
			}

			// Buffers go first, for alignment purposes.
			heap.heap_table_size[i] += num_image_descriptors * sizeof(uint32_t);
		}
		else
		{
			heap.image_strategies[i] = DescriptorStrategy::HeapSlice;
			heap.push_image_offsets[i] = push_data_offset;
			push_data_offset += sizeof(uint32_t);

			// Allocate N descriptors from the heap and write them directly.
			heap.heap_slice_size[i] = align(heap.heap_slice_size[i], image_desc_size);
			heap.heap_slice_size[i] = num_image_descriptors * image_desc_size;
		}
	}

	heap.push_data_size = push_data_offset;
	VK_ASSERT(heap.push_data_size <= VULKAN_PUSH_DATA_SIZE);
}

void PipelineLayout::init_legacy(const ImmutableSamplerBank *immutable_samplers)
{
	VkDescriptorSetLayout layouts[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	unsigned num_sets = 0;
	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		set_allocators[i] = device->request_descriptor_set_allocator(layout.sets[i], layout.stages_for_bindings[i],
																	 immutable_samplers ? immutable_samplers->samplers[i] : nullptr);
		layouts[i] = set_allocators[i]->get_layout_for_pool();
		if (layout.descriptor_set_mask & (1u << i))
		{
			num_sets = i + 1;

			// Assume the last set index in layout is the highest frequency update one, make that push descriptor if possible.
			// Only one descriptor set can be push descriptor.
			bool has_push_layout = set_allocators[i]->get_layout_for_push() != VK_NULL_HANDLE;
			if (has_push_layout)
				push_set_index = i;
		}
	}

	if (push_set_index != UINT32_MAX)
		layouts[push_set_index] = set_allocators[push_set_index]->get_layout_for_push();

	if (num_sets > VULKAN_NUM_DESCRIPTOR_SETS)
		LOGE("Number of sets %u exceeds limit of %u.\n", num_sets, VULKAN_NUM_DESCRIPTOR_SETS);

	VkPipelineLayoutCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	if (num_sets)
	{
		info.setLayoutCount = num_sets;
		info.pSetLayouts = layouts;
	}

	if (layout.push_constant_range.stageFlags != 0)
	{
		info.pushConstantRangeCount = 1;
		info.pPushConstantRanges = &layout.push_constant_range;
	}

#ifdef VULKAN_DEBUG
	LOGI("Creating pipeline layout.\n");
#endif
	auto &table = device->get_device_table();
	if (table.vkCreatePipelineLayout(device->get_device(), &info, nullptr, &pipe_layout) != VK_SUCCESS)
		LOGE("Failed to create pipeline layout.\n");
#ifdef GRANITE_VULKAN_FOSSILIZE
	device->register_pipeline_layout(pipe_layout, get_hash(), info);
#endif

	if (!device->get_device_features().descriptor_buffer_features.descriptorBuffer)
		create_update_templates();
}

PipelineLayout::PipelineLayout(Hash hash, Device *device_, const CombinedResourceLayout &layout_,
                               const ImmutableSamplerBank *immutable_samplers)
	: IntrusiveHashMapEnabled<PipelineLayout>(hash)
	, device(device_)
	, layout(layout_)
{
	if (device->get_device_features().descriptor_heap_features.descriptorHeap)
	{
		VK_ASSERT(!immutable_samplers);
		init_heap();
	}
	else
	{
		init_legacy(immutable_samplers);
	}
}

void PipelineLayout::create_update_templates()
{
	auto &table = device->get_device_table();
	for (unsigned desc_set = 0; desc_set < VULKAN_NUM_DESCRIPTOR_SETS; desc_set++)
	{
		if ((layout.descriptor_set_mask & (1u << desc_set)) == 0)
			continue;
		if ((layout.bindless_descriptor_set_mask & (1u << desc_set)) != 0)
			continue;

		VkDescriptorUpdateTemplateEntry update_entries[VULKAN_NUM_BINDINGS];
		uint32_t update_count = 0;

		auto &set_layout = layout.sets[desc_set];

		for_each_bit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			// Work around a RenderDoc capture bug where descriptorCount > 1 is not handled correctly.
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				entry.offset = offsetof(ResourceBinding, buffer) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.storage_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				entry.offset = offsetof(ResourceBinding, buffer) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.rtas_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				entry.offset = offsetof(ResourceBinding, rtas) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.sampled_texel_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				entry.offset = offsetof(ResourceBinding, buffer_view.handle) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.storage_texel_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				entry.offset = offsetof(ResourceBinding, buffer_view.handle) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.sampled_image_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				if (set_layout.fp_mask & (1u << binding))
					entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * (binding + i);
				else
					entry.offset = offsetof(ResourceBinding, image.integer) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.separate_image_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				if (set_layout.fp_mask & (1u << binding))
					entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * (binding + i);
				else
					entry.offset = offsetof(ResourceBinding, image.integer) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.sampler_mask & ~set_layout.immutable_sampler_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.storage_image_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		for_each_bit(set_layout.input_attachment_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.meta[binding].array_size;
			VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
			for (unsigned i = 0; i < array_size; i++)
			{
				auto &entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				entry.dstBinding = binding;
				entry.dstArrayElement = i;
				entry.descriptorCount = 1;
				if (set_layout.fp_mask & (1u << binding))
					entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * (binding + i);
				else
					entry.offset = offsetof(ResourceBinding, image.integer) + sizeof(ResourceBinding) * (binding + i);
				entry.stride = sizeof(ResourceBinding);
			}
		});

		VkDescriptorUpdateTemplateCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO };
		info.pipelineLayout = pipe_layout;

		if (desc_set == push_set_index)
		{
			info.descriptorSetLayout = set_allocators[desc_set]->get_layout_for_push();
			info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS;
		}
		else
		{
			info.descriptorSetLayout = set_allocators[desc_set]->get_layout_for_pool();
			info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
		}

		info.set = desc_set;
		info.descriptorUpdateEntryCount = update_count;
		info.pDescriptorUpdateEntries = update_entries;
		info.pipelineBindPoint = (layout.stages_for_sets[desc_set] & VK_SHADER_STAGE_COMPUTE_BIT) ?
				VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

		if (table.vkCreateDescriptorUpdateTemplate(device->get_device(), &info, nullptr,
		                                           &update_template[desc_set]) != VK_SUCCESS)
		{
			LOGE("Failed to create descriptor update template.\n");
		}
	}
}

PipelineLayout::~PipelineLayout()
{
	auto &table = device->get_device_table();
	if (pipe_layout != VK_NULL_HANDLE)
		table.vkDestroyPipelineLayout(device->get_device(), pipe_layout, nullptr);

	for (auto &update : update_template)
		if (update != VK_NULL_HANDLE)
			table.vkDestroyDescriptorUpdateTemplate(device->get_device(), update, nullptr);
}

const char *Shader::stage_to_name(ShaderStage stage)
{
	switch (stage)
	{
	case ShaderStage::Compute:
		return "compute";
	case ShaderStage::Vertex:
		return "vertex";
	case ShaderStage::Fragment:
		return "fragment";
	case ShaderStage::Task:
		return "task";
	case ShaderStage::Mesh:
		return "mesh";
	default:
		return "unknown";
	}
}

// Implicitly also checks for endian issues.
static const uint16_t reflection_magic[] = { 'G', 'R', 'A', ResourceLayout::Version };

size_t ResourceLayout::serialization_size()
{
	return sizeof(ResourceLayout) + sizeof(reflection_magic);
}

bool ResourceLayout::serialize(uint8_t *data, size_t size) const
{
	if (size != serialization_size())
		return false;

	// Cannot serialize externally defined immutable samplers.
	for (auto &set : sets)
		if (set.immutable_sampler_mask != 0)
			return false;

	memcpy(data, reflection_magic, sizeof(reflection_magic));
	memcpy(data + sizeof(reflection_magic), this, sizeof(*this));
	return true;
}

bool ResourceLayout::unserialize(const uint8_t *data, size_t size)
{
	if (size != sizeof(*this) + sizeof(reflection_magic))
	{
		LOGE("Reflection size mismatch.\n");
		return false;
	}

	if (memcmp(data, reflection_magic, sizeof(reflection_magic)) != 0)
	{
		LOGE("Magic mismatch.\n");
		return false;
	}

	memcpy(this, data + sizeof(reflection_magic), sizeof(*this));
	return true;
}

Util::Hash Shader::hash(const uint32_t *data, size_t size)
{
	Util::Hasher hasher;
	hasher.data(data, size);
	return hasher.get();
}

#ifdef GRANITE_VULKAN_SPIRV_CROSS
static void update_array_info(ResourceLayout &layout, const SPIRType &type, unsigned set, unsigned binding)
{
	auto &meta = layout.sets[set].meta[binding];

	if (!type.array.empty())
	{
		if (type.array.size() != 1)
			LOGE("Array dimension must be 1.\n");
		else if (!type.array_size_literal.front())
			LOGE("Array dimension must be a literal.\n");
		else
		{
			if (type.array.front() == 0)
			{
				if (binding != 0)
					LOGE("Bindless textures can only be used with binding = 0 in a set.\n");

				if (type.basetype != SPIRType::Image || type.image.dim == spv::DimBuffer)
				{
					LOGE("Can only use bindless for sampled images.\n");
				}
				else
				{
					layout.bindless_set_mask |= 1u << set;
					// Ignore fp_mask for bindless since we can mix and match.
					layout.sets[set].fp_mask = 0;
				}

				meta.array_size = DescriptorSetLayout::UNSIZED_ARRAY;
			}
			else if (meta.array_size && meta.array_size != type.array.front())
				LOGE("Array dimension for (%u, %u) is inconsistent.\n", set, binding);
			else if (type.array.front() + binding > VULKAN_NUM_BINDINGS)
				LOGE("Binding array will go out of bounds.\n");
			else
				meta.array_size = uint8_t(type.array.front());
		}
	}
	else
	{
		if (meta.array_size && meta.array_size != 1)
			LOGE("Array dimension for (%u, %u) is inconsistent.\n", set, binding);
		meta.array_size = 1;
	}
}

bool Shader::reflect_resource_layout(ResourceLayout &layout, const uint32_t *data, size_t size)
{
	Compiler compiler(data, size / sizeof(uint32_t));

#ifdef VULKAN_DEBUG
	LOGI("Reflecting shader layout.\n");
#endif

	auto resources = compiler.get_shader_resources();
	for (auto &image : resources.sampled_images)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		auto &type = compiler.get_type(image.type_id);
		if (type.image.dim == spv::DimBuffer)
			layout.sets[set].sampled_texel_buffer_mask |= 1u << binding;
		else
			layout.sets[set].sampled_image_mask |= 1u << binding;

		if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
			layout.sets[set].fp_mask |= 1u << binding;

		update_array_info(layout, type, set, binding);
	}

	for (auto &image : resources.subpass_inputs)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		layout.sets[set].input_attachment_mask |= 1u << binding;

		auto &type = compiler.get_type(image.type_id);
		if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
			layout.sets[set].fp_mask |= 1u << binding;
		update_array_info(layout, type, set, binding);
	}

	for (auto &image : resources.separate_images)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		auto &type = compiler.get_type(image.type_id);
		if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
			layout.sets[set].fp_mask |= 1u << binding;

		if (type.image.dim == spv::DimBuffer)
			layout.sets[set].sampled_texel_buffer_mask |= 1u << binding;
		else
			layout.sets[set].separate_image_mask |= 1u << binding;

		update_array_info(layout, type, set, binding);
	}

	for (auto &image : resources.separate_samplers)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		layout.sets[set].sampler_mask |= 1u << binding;
		update_array_info(layout, compiler.get_type(image.type_id), set, binding);
	}

	for (auto &image : resources.storage_images)
	{
		auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		auto &type = compiler.get_type(image.type_id);
		if (type.image.dim == spv::DimBuffer)
			layout.sets[set].storage_texel_buffer_mask |= 1u << binding;
		else
			layout.sets[set].storage_image_mask |= 1u << binding;

		if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
			layout.sets[set].fp_mask |= 1u << binding;

		update_array_info(layout, type, set, binding);
	}

	for (auto &buffer : resources.uniform_buffers)
	{
		auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		layout.sets[set].uniform_buffer_mask |= 1u << binding;
		update_array_info(layout, compiler.get_type(buffer.type_id), set, binding);
	}

	for (auto &buffer : resources.storage_buffers)
	{
		auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		layout.sets[set].storage_buffer_mask |= 1u << binding;
		update_array_info(layout, compiler.get_type(buffer.type_id), set, binding);
	}

	for (auto &buffer : resources.acceleration_structures)
	{
		auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
		auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		layout.sets[set].rtas_mask |= 1u << binding;
		update_array_info(layout, compiler.get_type(buffer.type_id), set, binding);
	}

	for (auto &attrib : resources.stage_inputs)
	{
		auto location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
		layout.input_mask |= 1u << location;
	}

	for (auto &attrib : resources.stage_outputs)
	{
		auto location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
		layout.output_mask |= 1u << location;
	}

	if (!resources.push_constant_buffers.empty())
	{
		// Don't bother trying to extract which part of a push constant block we're using.
		// Just assume we're accessing everything. At least on older validation layers,
		// it did not do a static analysis to determine similar information, so we got a lot
		// of false positives.
		layout.push_constant_size =
				compiler.get_declared_struct_size(compiler.get_type(resources.push_constant_buffers.front().base_type_id));
	}

	auto spec_constants = compiler.get_specialization_constants();
	for (auto &c : spec_constants)
	{
		if (c.constant_id >= VULKAN_NUM_TOTAL_SPEC_CONSTANTS)
		{
			LOGE("Spec constant ID: %u is out of range, will be ignored.\n", c.constant_id);
			continue;
		}

		layout.spec_constant_mask |= 1u << c.constant_id;
	}

	return true;
}
#else
bool Shader::reflect_resource_layout(ResourceLayout &, const uint32_t *, size_t)
{
	return false;
}
#endif

Shader::Shader(Hash hash, Device *device_, const uint32_t *data, size_t size,
               const ResourceLayout *resource_layout)
	: IntrusiveHashMapEnabled<Shader>(hash)
	, device(device_)
{
	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	info.codeSize = size;
	info.pCode = data;

#ifdef VULKAN_DEBUG
	LOGI("Creating shader module.\n");
#endif
	auto &table = device->get_device_table();
	if (table.vkCreateShaderModule(device->get_device(), &info, nullptr, &module) != VK_SUCCESS)
		LOGE("Failed to create shader module.\n");

#ifdef HAVE_GRANITE_VULKAN_POST_MORTEM
	PostMortem::register_shader(data, size);
#endif

#ifdef GRANITE_VULKAN_FOSSILIZE
	device->register_shader_module(module, get_hash(), info);
#endif

	if (resource_layout)
		layout = *resource_layout;
#ifdef GRANITE_VULKAN_SPIRV_CROSS
	else if (!reflect_resource_layout(layout, data, size))
		LOGE("Failed to reflect resource layout.\n");
#endif

	if (layout.bindless_set_mask != 0 && !device->get_device_features().vk12_features.descriptorIndexing)
		LOGE("Sufficient features for descriptor indexing is not supported on this device.\n");
}

Shader::~Shader()
{
	auto &table = device->get_device_table();
	if (module)
		table.vkDestroyShaderModule(device->get_device(), module, nullptr);
}

void Program::set_shader(ShaderStage stage, Shader *handle)
{
	shaders[Util::ecast(stage)] = handle;
}

Program::Program(Device *device_, Shader *vertex, Shader *fragment, const ImmutableSamplerBank *sampler_bank)
    : device(device_)
{
	set_shader(ShaderStage::Vertex, vertex);
	set_shader(ShaderStage::Fragment, fragment);
	device->bake_program(*this, sampler_bank);
}

Program::Program(Device *device_, Shader *task, Shader *mesh, Shader *fragment, const ImmutableSamplerBank *sampler_bank)
	: device(device_)
{
	if (task)
		set_shader(ShaderStage::Task, task);
	set_shader(ShaderStage::Mesh, mesh);
	set_shader(ShaderStage::Fragment, fragment);
	device->bake_program(*this, sampler_bank);
}

Program::Program(Device *device_, Shader *compute_shader, const ImmutableSamplerBank *sampler_bank)
    : device(device_)
{
	set_shader(ShaderStage::Compute, compute_shader);
	device->bake_program(*this, sampler_bank);
}

Pipeline Program::get_pipeline(Hash hash) const
{
	auto *ret = pipelines.find(hash);
	return ret ? ret->get() : Pipeline{};
}

Pipeline Program::add_pipeline(Hash hash, const Pipeline &pipeline)
{
	return pipelines.emplace_yield(hash, pipeline)->get();
}

void Program::destroy_pipeline(const Pipeline &pipeline)
{
	device->get_device_table().vkDestroyPipeline(device->get_device(), pipeline.pipeline, nullptr);
}

void Program::promote_read_write_to_read_only()
{
	pipelines.move_to_read_only();
}

Program::~Program()
{
	for (auto &pipe : pipelines.get_read_only())
		destroy_pipeline(pipe.get());
	for (auto &pipe : pipelines.get_read_write())
		destroy_pipeline(pipe.get());
}
}
