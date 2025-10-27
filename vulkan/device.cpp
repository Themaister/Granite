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
#include "device.hpp"
#ifdef GRANITE_VULKAN_FOSSILIZE
#include "device_fossilize.hpp"
#endif
#include "format.hpp"
#include "timeline_trace_file.hpp"
#include "type_to_string.hpp"
#include "quirks.hpp"
#include "timer.hpp"
#include <algorithm>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
#include "string_helpers.hpp"
#endif

#include "thread_id.hpp"
static unsigned get_thread_index()
{
	return Util::get_current_thread_index();
}
#define LOCK() std::lock_guard<std::mutex> _holder_##__COUNTER__{lock.lock}
#define LOCK_MEMORY() std::lock_guard<std::mutex> _holder_##__COUNTER__{lock.memory_lock}
#define LOCK_CACHE() ::Util::RWSpinLockReadHolder _holder_##__COUNTER__{lock.read_only_cache}
#define DRAIN_FRAME_LOCK() \
	std::unique_lock<std::mutex> _holder{lock.lock}; \
	lock.cond.wait(_holder, [&]() { \
		return lock.counter == 0; \
	})

using namespace Util;

namespace Vulkan
{
static constexpr VkImageUsageFlags image_usage_video_flags =
		VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR |
		VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR |
		VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR |
		VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
		VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR |
		VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;

static const QueueIndices queue_flush_order[] = {
	QUEUE_INDEX_TRANSFER,
	QUEUE_INDEX_VIDEO_DECODE,
	QUEUE_INDEX_VIDEO_ENCODE,
	QUEUE_INDEX_GRAPHICS,
	QUEUE_INDEX_COMPUTE,
};

Device::Device()
    : framebuffer_allocator(this)
    , transient_allocator(this)
	, pipeline_binary_cache(this)
#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
	, shader_manager(this)
	, resource_manager(this)
#endif
{
	cookie.store(0);
}

Semaphore Device::request_semaphore(VkSemaphoreType type, VkSemaphore vk_semaphore, bool transfer_ownership)
{
	if (type == VK_SEMAPHORE_TYPE_TIMELINE && !ext.vk12_features.timelineSemaphore)
	{
		LOGE("Timeline semaphores not supported.\n");
		return Semaphore{};
	}

	if (vk_semaphore == VK_NULL_HANDLE)
	{
		if (type == VK_SEMAPHORE_TYPE_BINARY)
		{
			LOCK();
			vk_semaphore = managers.semaphore.request_cleared_semaphore();
		}
		else
		{
			VkSemaphoreTypeCreateInfo type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
			VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			info.pNext = &type_info;
			type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
			type_info.initialValue = 0;
			if (table->vkCreateSemaphore(device, &info, nullptr, &vk_semaphore) != VK_SUCCESS)
			{
				LOGE("Failed to create semaphore.\n");
				return Semaphore{};
			}
		}
		transfer_ownership = true;
	}

	if (type == VK_SEMAPHORE_TYPE_BINARY)
	{
		Semaphore ptr(handle_pool.semaphores.allocate(this, vk_semaphore, false, transfer_ownership));
		return ptr;
	}
	else
	{
		Semaphore ptr(handle_pool.semaphores.allocate(this, 0, vk_semaphore, transfer_ownership));
		ptr->set_proxy_timeline();
		return ptr;
	}
}

Semaphore Device::request_timeline_semaphore_as_binary(const SemaphoreHolder &holder, uint64_t value)
{
	VK_ASSERT(holder.get_semaphore_type() == VK_SEMAPHORE_TYPE_TIMELINE);
	VK_ASSERT(holder.is_proxy_timeline());
	Semaphore ptr(handle_pool.semaphores.allocate(this, value, holder.get_semaphore(), false));
	return ptr;
}

Semaphore Device::request_semaphore_external(VkSemaphoreType type,
                                             VkExternalSemaphoreHandleTypeFlagBits handle_type)
{
	if (type == VK_SEMAPHORE_TYPE_TIMELINE && !ext.vk12_features.timelineSemaphore)
	{
		LOGE("Timeline semaphores not supported.\n");
		return Semaphore{};
	}

	if (!ext.supports_external)
	{
		LOGE("External semaphores not supported.\n");
		return Semaphore{};
	}

	VkSemaphoreTypeCreateInfo type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
	type_info.semaphoreType = type;
	VkExternalSemaphoreFeatureFlags features;

	{
		VkExternalSemaphoreProperties props = { VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES };
		VkPhysicalDeviceExternalSemaphoreInfo info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO };
		info.handleType = handle_type;

		// Workaround AMD Windows bug where it reports TIMELINE as not supported.
		// D3D12_FENCE used to be BINARY type before timelines were introduced to Vulkan.
		if (type != VK_SEMAPHORE_TYPE_BINARY && handle_type != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
			info.pNext = &type_info;
		vkGetPhysicalDeviceExternalSemaphoreProperties(gpu, &info, &props);

		features = props.externalSemaphoreFeatures;

		if (!features)
		{
			LOGE("External semaphore handle type #%x is not supported.\n", handle_type);
			return Semaphore{};
		}
	}

	VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkExportSemaphoreCreateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };

	if ((features & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) != 0)
	{
		export_info.handleTypes = handle_type;
		export_info.pNext = info.pNext;
		info.pNext = &export_info;
	}

	if (type != VK_SEMAPHORE_TYPE_BINARY)
	{
		type_info.pNext = info.pNext;
		info.pNext = &type_info;
	}

	VkSemaphore semaphore;
	if (table->vkCreateSemaphore(device, &info, nullptr, &semaphore) != VK_SUCCESS)
	{
		LOGE("Failed to create external semaphore.\n");
		return Semaphore{};
	}

	if (type == VK_SEMAPHORE_TYPE_TIMELINE)
	{
		Semaphore ptr(handle_pool.semaphores.allocate(this, 0, semaphore, true));
		ptr->set_external_object_compatible(handle_type, features);
		ptr->set_proxy_timeline();
		return ptr;
	}
	else
	{
		Semaphore ptr(handle_pool.semaphores.allocate(this, semaphore, false, true));
		ptr->set_external_object_compatible(handle_type, features);
		return ptr;
	}
}

Semaphore Device::request_proxy_semaphore()
{
	Semaphore ptr(handle_pool.semaphores.allocate(this));
	return ptr;
}

void Device::add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags2 stages, bool flush)
{
	VK_ASSERT(!semaphore->is_proxy_timeline());

	LOCK();
	add_wait_semaphore_nolock(get_physical_queue_type(type), std::move(semaphore), stages, flush);
}

void Device::add_wait_semaphore_nolock(QueueIndices physical_type, Semaphore semaphore,
                                       VkPipelineStageFlags2 stages, bool flush)
{
	if (flush)
		flush_frame_nolock(physical_type);
	auto &data = queue_data[physical_type];

#ifdef VULKAN_DEBUG
	for (auto &sem : data.wait_semaphores)
		VK_ASSERT(sem.get() != semaphore.get());
#endif

	semaphore->set_pending_wait();
	data.wait_semaphores.push_back(semaphore);
	data.wait_stages.push_back(stages);
	data.need_fence = true;

	// Sanity check.
	VK_ASSERT(data.wait_semaphores.size() < 16 * 1024);
}

LinearHostImageHandle Device::create_linear_host_image(const LinearHostImageCreateInfo &info)
{
	if ((info.usage & ~VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
		return LinearHostImageHandle(nullptr);

	ImageCreateInfo create_info;
	create_info.width = info.width;
	create_info.height = info.height;
	create_info.domain =
			(info.flags & LINEAR_HOST_IMAGE_HOST_CACHED_BIT) != 0 ?
			ImageDomain::LinearHostCached :
			ImageDomain::LinearHost;
	create_info.levels = 1;
	create_info.layers = 1;
	create_info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	create_info.format = info.format;
	create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	create_info.usage = info.usage;
	create_info.type = VK_IMAGE_TYPE_2D;

	if ((info.flags & LINEAR_HOST_IMAGE_REQUIRE_LINEAR_FILTER_BIT) != 0)
		create_info.misc |= IMAGE_MISC_VERIFY_FORMAT_FEATURE_SAMPLED_LINEAR_FILTER_BIT;
	if ((info.flags & LINEAR_HOST_IMAGE_IGNORE_DEVICE_LOCAL_BIT) != 0)
		create_info.misc |= IMAGE_MISC_LINEAR_IMAGE_IGNORE_DEVICE_LOCAL_BIT;

	BufferHandle cpu_image;
	auto gpu_image = create_image(create_info);
	if (!gpu_image)
	{
		// Fall-back to staging buffer.
		create_info.domain = ImageDomain::Physical;
		create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		create_info.misc = IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT;
		create_info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		gpu_image = create_image(create_info);
		if (!gpu_image)
			return LinearHostImageHandle(nullptr);

		BufferCreateInfo buffer;
		buffer.domain =
				(info.flags & LINEAR_HOST_IMAGE_HOST_CACHED_BIT) != 0 ?
				BufferDomain::CachedHost :
				BufferDomain::Host;
		buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		buffer.size = info.width * info.height * TextureFormatLayout::format_block_size(info.format, format_to_aspect_mask(info.format));
		cpu_image = create_buffer(buffer);
		if (!cpu_image)
			return LinearHostImageHandle(nullptr);
	}
	else
		gpu_image->set_layout(Layout::General);

	return LinearHostImageHandle(handle_pool.linear_images.allocate(this, std::move(gpu_image), std::move(cpu_image), info.stages));
}

void *Device::map_linear_host_image(const LinearHostImage &image, MemoryAccessFlags access)
{
	void *host = managers.memory.map_memory(image.get_host_visible_allocation(), access,
	                                        0, image.get_host_visible_allocation().get_size());
	return host;
}

void Device::unmap_linear_host_image_and_sync(const LinearHostImage &image, MemoryAccessFlags access)
{
	managers.memory.unmap_memory(image.get_host_visible_allocation(), access,
	                             0, image.get_host_visible_allocation().get_size());
	if (image.need_staging_copy())
	{
		// Kinda icky fallback, shouldn't really be used on discrete cards.
		auto cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		cmd->image_barrier(image.get_image(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_NONE, 0,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		cmd->copy_buffer_to_image(image.get_image(), image.get_host_visible_buffer(),
		                          0, {},
		                          { image.get_image().get_width(), image.get_image().get_height(), 1 },
		                          0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

		// Don't care about dstAccessMask, semaphore takes care of everything.
		cmd->image_barrier(image.get_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_NONE, 0);

		Semaphore sem;
		submit(cmd, nullptr, 1, &sem);

		// The queue type is an assumption. Should add some parameter for that.
		add_wait_semaphore(CommandBuffer::Type::Generic, sem, image.get_used_pipeline_stages(), true);
	}
}

void *Device::map_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
{
	void *host = managers.memory.map_memory(buffer.get_allocation(), access, 0, buffer.get_create_info().size);
	return host;
}

void Device::unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
{
	managers.memory.unmap_memory(buffer.get_allocation(), access, 0, buffer.get_create_info().size);
}

void *Device::map_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length)
{
	VK_ASSERT(offset + length <= buffer.get_create_info().size);
	void *host = managers.memory.map_memory(buffer.get_allocation(), access, offset, length);
	return host;
}

void Device::unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length)
{
	VK_ASSERT(offset + length <= buffer.get_create_info().size);
	managers.memory.unmap_memory(buffer.get_allocation(), access, offset, length);
}

Shader *Device::request_shader(const uint32_t *data, size_t size, const ResourceLayout *layout)
{
	auto hash = Shader::hash(data, size);
	LOCK_CACHE();
	auto *ret = shaders.find(hash);
	if (!ret)
		ret = shaders.emplace_yield(hash, hash, this, data, size, layout);
	return ret;
}

Shader *Device::request_shader_by_hash(Hash hash)
{
	LOCK_CACHE();
	return shaders.find(hash);
}

Program *Device::request_program(Vulkan::Shader *compute_shader, const ImmutableSamplerBank *sampler_bank)
{
	if (!compute_shader)
		return nullptr;

	Util::Hasher hasher;
	hasher.u64(compute_shader->get_hash());
	ImmutableSamplerBank::hash(hasher, sampler_bank);

	LOCK_CACHE();
	auto hash = hasher.get();
	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, this, compute_shader, sampler_bank);
	return ret;
}

Program *Device::request_program(const uint32_t *compute_data, size_t compute_size, const ResourceLayout *layout)
{
	if (!compute_size)
		return nullptr;

	auto *compute_shader = request_shader(compute_data, compute_size, layout);
	return request_program(compute_shader);
}

Program *Device::request_program(Shader *vertex, Shader *fragment, const ImmutableSamplerBank *sampler_bank)
{
	if (!vertex || !fragment)
		return nullptr;

	Util::Hasher hasher;
	hasher.u64(vertex->get_hash());
	hasher.u64(fragment->get_hash());
	ImmutableSamplerBank::hash(hasher, sampler_bank);

	auto hash = hasher.get();
	LOCK_CACHE();
	auto *ret = programs.find(hash);

	if (!ret)
		ret = programs.emplace_yield(hash, this, vertex, fragment, sampler_bank);
	return ret;
}

Program *Device::request_program(Shader *task, Shader *mesh, Shader *fragment, const ImmutableSamplerBank *sampler_bank)
{
	if (!mesh || !fragment)
		return nullptr;

	if (!get_device_features().mesh_shader_features.meshShader)
	{
		LOGE("meshShader not supported.\n");
		return nullptr;
	}

	if (task && !get_device_features().mesh_shader_features.taskShader)
	{
		LOGE("taskShader not supported.\n");
		return nullptr;
	}

	Util::Hasher hasher;
	hasher.u64(task ? task->get_hash() : 0);
	hasher.u64(mesh->get_hash());
	hasher.u64(fragment->get_hash());
	ImmutableSamplerBank::hash(hasher, sampler_bank);

	auto hash = hasher.get();
	LOCK_CACHE();
	auto *ret = programs.find(hash);

	if (!ret)
		ret = programs.emplace_yield(hash, this, task, mesh, fragment, sampler_bank);
	return ret;
}

Program *Device::request_program(const uint32_t *vertex_data, size_t vertex_size,
                                 const uint32_t *fragment_data, size_t fragment_size,
                                 const ResourceLayout *vertex_layout,
                                 const ResourceLayout *fragment_layout)
{
	if (!vertex_size || !fragment_size)
		return nullptr;

	auto *vertex = request_shader(vertex_data, vertex_size, vertex_layout);
	auto *fragment = request_shader(fragment_data, fragment_size, fragment_layout);
	return request_program(vertex, fragment);
}

Program *Device::request_program(const uint32_t *task_data, size_t task_size,
                                 const uint32_t *mesh_data, size_t mesh_size,
                                 const uint32_t *fragment_data, size_t fragment_size,
                                 const ResourceLayout *task_layout,
                                 const ResourceLayout *mesh_layout,
                                 const ResourceLayout *fragment_layout)
{
	if (!mesh_size || !fragment_size)
		return nullptr;

	Shader *task = nullptr;
	if (task_size)
		task = request_shader(task_data, task_size, task_layout);
	auto *mesh = request_shader(mesh_data, mesh_size, mesh_layout);
	auto *fragment = request_shader(fragment_data, fragment_size, fragment_layout);
	return request_program(task, mesh, fragment);
}

const PipelineLayout *Device::request_pipeline_layout(const CombinedResourceLayout &layout,
                                                      const ImmutableSamplerBank *sampler_bank)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(layout.sets), sizeof(layout.sets));
	h.data(&layout.stages_for_bindings[0][0], sizeof(layout.stages_for_bindings));
	h.u32(layout.push_constant_range.stageFlags);
	h.u32(layout.push_constant_range.size);
	h.data(layout.spec_constant_mask, sizeof(layout.spec_constant_mask));
	h.u32(layout.attribute_mask);
	h.u32(layout.render_target_mask);
	for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
	{
		Util::for_each_bit(layout.sets[set].immutable_sampler_mask, [&](unsigned bit) {
			VK_ASSERT(sampler_bank && sampler_bank->samplers[set][bit]);
			h.u64(sampler_bank->samplers[set][bit]->get_hash());
		});
	}

	auto hash = h.get();
	auto *ret = pipeline_layouts.find(hash);
	if (!ret)
		ret = pipeline_layouts.emplace_yield(hash, hash, this, layout, sampler_bank);
	return ret;
}

DescriptorSetAllocator *Device::request_descriptor_set_allocator(const DescriptorSetLayout &layout, const uint32_t *stages_for_bindings,
                                                                 const ImmutableSampler * const *immutable_samplers_)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(&layout), sizeof(layout));
	h.data(stages_for_bindings, sizeof(uint32_t) * VULKAN_NUM_BINDINGS);
	Util::for_each_bit(layout.immutable_sampler_mask, [&](unsigned bit) {
		VK_ASSERT(immutable_samplers_ && immutable_samplers_[bit]);
		h.u64(immutable_samplers_[bit]->get_hash());
	});
	auto hash = h.get();

	LOCK_CACHE();
	auto *ret = descriptor_set_allocators.find(hash);
	if (!ret)
		ret = descriptor_set_allocators.emplace_yield(hash, hash, this, layout, stages_for_bindings, immutable_samplers_);
	return ret;
}

const IndirectLayout *Device::request_indirect_layout(
		const PipelineLayout *layout, const Vulkan::IndirectLayoutToken *tokens,
		uint32_t num_tokens, uint32_t stride)
{
	Hasher h;

	h.u64(layout ? layout->get_hash() : 0);

	for (uint32_t i = 0; i < num_tokens; i++)
		h.u32(Util::ecast(tokens[i].type));

	for (uint32_t i = 0; i < num_tokens; i++)
	{
		if (tokens[i].type != IndirectLayoutToken::Type::SequenceCount)
			h.u32(tokens[i].offset);

		if (tokens[i].type == IndirectLayoutToken::Type::PushConstant ||
		    tokens[i].type == IndirectLayoutToken::Type::SequenceCount)
		{
			h.u32(tokens[i].data.push.offset);
			h.u32(tokens[i].data.push.range);
		}
		else if (tokens[i].type == IndirectLayoutToken::Type::VBO)
		{
			h.u32(tokens[i].data.vbo.binding);
		}
	}

	h.u32(stride);
	auto hash = h.get();

	LOCK_CACHE();
	auto *ret = indirect_layouts.find(hash);
	if (!ret)
		ret = indirect_layouts.emplace_yield(hash, this, layout, tokens, num_tokens, stride);
	return ret;
}

void Device::merge_combined_resource_layout(CombinedResourceLayout &layout, const Program &program)
{
	if (program.get_shader(ShaderStage::Vertex))
		layout.attribute_mask |= program.get_shader(ShaderStage::Vertex)->get_layout().input_mask;
	if (program.get_shader(ShaderStage::Fragment))
		layout.render_target_mask |= program.get_shader(ShaderStage::Fragment)->get_layout().output_mask;

	for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
	{
		auto *shader = program.get_shader(static_cast<ShaderStage>(i));
		if (!shader)
			continue;

		uint32_t stage_mask = 1u << i;

		auto &shader_layout = shader->get_layout();

		for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
		{
			layout.sets[set].sampled_image_mask |= shader_layout.sets[set].sampled_image_mask;
			layout.sets[set].storage_image_mask |= shader_layout.sets[set].storage_image_mask;
			layout.sets[set].uniform_buffer_mask |= shader_layout.sets[set].uniform_buffer_mask;
			layout.sets[set].storage_buffer_mask |= shader_layout.sets[set].storage_buffer_mask;
			layout.sets[set].sampled_texel_buffer_mask |= shader_layout.sets[set].sampled_texel_buffer_mask;
			layout.sets[set].storage_texel_buffer_mask |= shader_layout.sets[set].storage_texel_buffer_mask;
			layout.sets[set].input_attachment_mask |= shader_layout.sets[set].input_attachment_mask;
			layout.sets[set].sampler_mask |= shader_layout.sets[set].sampler_mask;
			layout.sets[set].separate_image_mask |= shader_layout.sets[set].separate_image_mask;
			layout.sets[set].fp_mask |= shader_layout.sets[set].fp_mask;

			uint32_t active_binds =
					shader_layout.sets[set].sampled_image_mask |
					shader_layout.sets[set].storage_image_mask |
					shader_layout.sets[set].uniform_buffer_mask|
					shader_layout.sets[set].storage_buffer_mask |
					shader_layout.sets[set].sampled_texel_buffer_mask |
					shader_layout.sets[set].storage_texel_buffer_mask |
					shader_layout.sets[set].input_attachment_mask |
					shader_layout.sets[set].sampler_mask |
					shader_layout.sets[set].separate_image_mask;

			if (active_binds)
				layout.stages_for_sets[set] |= stage_mask;

			for_each_bit(active_binds, [&](uint32_t bit) {
				layout.stages_for_bindings[set][bit] |= stage_mask;

				auto &combined_size = layout.sets[set].array_size[bit];
				auto &shader_size = shader_layout.sets[set].array_size[bit];
				if (combined_size && combined_size != shader_size)
					LOGE("Mismatch between array sizes in different shaders.\n");
				else
					combined_size = shader_size;
			});
		}

		// Merge push constant ranges into one range.
		// Do not try to split into multiple ranges as it just complicates things for no obvious gain.
		if (shader_layout.push_constant_size != 0)
		{
			layout.push_constant_range.stageFlags |= 1u << i;
			layout.push_constant_range.size =
					std::max(layout.push_constant_range.size, shader_layout.push_constant_size);
		}

		layout.spec_constant_mask[i] = shader_layout.spec_constant_mask;
		layout.combined_spec_constant_mask |= shader_layout.spec_constant_mask;
		layout.bindless_descriptor_set_mask |= shader_layout.bindless_set_mask;
	}

	for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
	{
		if (layout.stages_for_sets[set] == 0)
			continue;

		layout.descriptor_set_mask |= 1u << set;

		for (unsigned binding = 0; binding < VULKAN_NUM_BINDINGS; binding++)
		{
			auto &array_size = layout.sets[set].array_size[binding];
			if (array_size == DescriptorSetLayout::UNSIZED_ARRAY)
			{
				for (unsigned i = 1; i < VULKAN_NUM_BINDINGS; i++)
				{
					if (layout.stages_for_bindings[set][i] != 0)
						LOGE("Using bindless for set = %u, but binding = %u has a descriptor attached to it.\n", set, i);
				}

				// Allows us to have one unified descriptor set layout for bindless.
				layout.stages_for_bindings[set][binding] = VK_SHADER_STAGE_ALL;
			}
			else if (array_size == 0)
			{
				array_size = 1;
			}
			else
			{
				for (unsigned i = 1; i < array_size; i++)
				{
					if (layout.stages_for_bindings[set][binding + i] != 0)
					{
						LOGE("Detected binding aliasing for (%u, %u). Binding array with %u elements starting at (%u, %u) overlaps.\n",
							 set, binding + i, array_size, set, binding);
					}
				}
			}
		}
	}

	Hasher h;
	h.u32(layout.push_constant_range.stageFlags);
	h.u32(layout.push_constant_range.size);
	layout.push_constant_layout_hash = h.get();
}

void Device::bake_program(Program &program, const ImmutableSamplerBank *sampler_bank)
{
	CombinedResourceLayout layout;
	ImmutableSamplerBank ext_immutable_samplers = {};

	merge_combined_resource_layout(layout, program);

	if (sampler_bank)
	{
		for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
		{
			for_each_bit(layout.sets[set].sampler_mask | layout.sets[set].sampled_image_mask, [&](uint32_t binding)
			{
				if (sampler_bank->samplers[set][binding])
				{
					ext_immutable_samplers.samplers[set][binding] = sampler_bank->samplers[set][binding];
					layout.sets[set].immutable_sampler_mask |= 1u << binding;
				}
			});
		}
	}

	program.set_pipeline_layout(request_pipeline_layout(layout, &ext_immutable_samplers));
}

bool Device::init_pipeline_cache(const uint8_t *data, size_t size, bool persistent_mapping)
{
	if (ext.pipeline_binary_features.pipelineBinaries)
		return pipeline_binary_cache.init_from_payload(data, size, persistent_mapping);

	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	static const auto hash_size = sizeof(Util::Hash);

	VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	if (!data || size < uuid_size + hash_size)
	{
		LOGI("Creating a fresh pipeline cache.\n");
	}
	else if (memcmp(data, gpu_props.pipelineCacheUUID, uuid_size) != 0)
	{
		LOGI("Pipeline cache UUID changed.\n");
	}
	else
	{
		Util::Hash reference_hash;
		memcpy(&reference_hash, data + uuid_size, sizeof(reference_hash));

		info.initialDataSize = size - uuid_size - hash_size;
		data += uuid_size + hash_size;
		info.pInitialData = data;

		Util::Hasher h;
		h.data(data, info.initialDataSize);

		if (h.get() == reference_hash)
			LOGI("Initializing pipeline cache.\n");
		else
		{
			LOGW("Pipeline cache is corrupt, creating a fresh cache.\n");
			info.pInitialData = nullptr;
			info.initialDataSize = 0;
		}
	}

	if (legacy_pipeline_cache != VK_NULL_HANDLE)
		table->vkDestroyPipelineCache(device, legacy_pipeline_cache, nullptr);
	legacy_pipeline_cache = VK_NULL_HANDLE;
	return table->vkCreatePipelineCache(device, &info, nullptr, &legacy_pipeline_cache) == VK_SUCCESS;
}

void Device::init_pipeline_cache()
{
#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
	if (!system_handles.filesystem)
		return;
	auto file = system_handles.filesystem->open_readonly_mapping("cache://pipeline_cache.bin");
	if (file)
	{
		if (ext.pipeline_binary_features.pipelineBinaries)
			persistent_pipeline_cache = file;

		auto size = file->get_size();
		auto *mapped = file->data<uint8_t>();
		if (mapped && !init_pipeline_cache(mapped, size, bool(persistent_pipeline_cache)))
		{
			LOGE("Failed to initialize pipeline cache.\n");
			persistent_pipeline_cache.reset();
		}
	}
	else if (!init_pipeline_cache(nullptr, 0))
		LOGE("Failed to initialize pipeline cache.\n");
#endif
}

size_t Device::get_pipeline_cache_size()
{
	if (legacy_pipeline_cache == VK_NULL_HANDLE)
		return pipeline_binary_cache.get_serialized_size();

	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	static const auto hash_size = sizeof(Util::Hash);
	size_t size = 0;
	if (table->vkGetPipelineCacheData(device, legacy_pipeline_cache, &size, nullptr) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return 0;
	}

	return size + uuid_size + hash_size;
}

bool Device::get_pipeline_cache_data(uint8_t *data, size_t size)
{
	if (legacy_pipeline_cache == VK_NULL_HANDLE)
		return pipeline_binary_cache.serialize(data, size);

	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	static const auto hash_size = sizeof(Util::Hash);
	if (size < uuid_size + hash_size)
		return false;

	auto *hash_data = data + uuid_size;

	size -= uuid_size + hash_size;
	memcpy(data, gpu_props.pipelineCacheUUID, uuid_size);
	data = hash_data + hash_size;

	if (table->vkGetPipelineCacheData(device, legacy_pipeline_cache, &size, data) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return false;
	}

	Util::Hasher h;
	h.data(data, size);
	auto blob_hash = h.get();
	memcpy(hash_data, &blob_hash, sizeof(blob_hash));

	return true;
}

void Device::flush_pipeline_cache()
{
#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
	if (!system_handles.filesystem)
		return;

	if (ext.pipeline_binary_features.pipelineBinaries &&
	    !pipeline_binary_cache.has_new_binary_entries() &&
	    persistent_pipeline_cache)
	{
		LOGI("No new pipelines have been observed, skipping serialize.\n");
		return;
	}

	size_t size = get_pipeline_cache_size();
	if (!size)
	{
		LOGE("Failed to get pipeline cache size.\n");
		return;
	}

	auto file = system_handles.filesystem->open_transactional_mapping(
			"cache://pipeline_cache.bin", size);

	if (!file)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}

	if (!get_pipeline_cache_data(file->mutable_data<uint8_t>(), size))
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}

	persistent_pipeline_cache.reset();
#endif
}

void Device::init_workarounds()
{
	workarounds = {};

#ifdef __APPLE__
	// Events are not supported in MoltenVK.
	// TODO: Use VK_KHR_portability_subset to determine this.
	workarounds.emulate_event_as_pipeline_barrier = true;
	LOGW("Emulating events as pipeline barriers on Metal emulation.\n");
	LOGW("Disabling push descriptors on Metal emulation.\n");
#else
	bool sync2_workarounds = false;
	const bool mesa_driver = ext.driver_id == VK_DRIVER_ID_MESA_RADV ||
	                         ext.driver_id == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA ||
	                         ext.driver_id == VK_DRIVER_ID_MESA_TURNIP;
	const bool amd_driver = ext.driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
	                        ext.driver_id == VK_DRIVER_ID_AMD_PROPRIETARY;

	// AMD_PROPRIETARY was likely fixed before this, but fix was observed in this version (23.10.2).
	if (mesa_driver && gpu_props.driverVersion < VK_MAKE_VERSION(23, 1, 0))
		sync2_workarounds = true;
	else if (amd_driver && gpu_props.driverVersion < VK_MAKE_VERSION(2, 0, 283))
		sync2_workarounds = true;

	if (gpu_props.vendorID == VENDOR_ID_ARM)
	{
		LOGW("Workaround applied: Emulating events as pipeline barriers.\n");
		workarounds.emulate_event_as_pipeline_barrier = true;
	}

	// For whatever ridiculous reason, pipeline cache control causes GPU hangs on Pascal cards in parallel-rdp.
	// Use mesh shaders as the sentinel to check for that.
	if (ext.driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
	    (gpu_props.driverVersion < VK_VERSION_MAJOR(535) ||
	     !ext.mesh_shader_features.meshShader))
	{
		LOGW("Disabling pipeline cache control.\n");
		workarounds.broken_pipeline_cache_control = true;
	}
	else if (ext.driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY_KHR)
	{
		// Seems broken on this driver too. Compilation stutter galore ...
		LOGW("Disabling pipeline cache control.\n");
		workarounds.broken_pipeline_cache_control = true;
	}

	if (sync2_workarounds)
	{
		LOGW("Enabling workaround for sync2 access mask bugs.\n");
		// https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21271
		// Found bug around 23.0. Should be fixed by 23.1.
		// Also observed on AMD windows. Probably fails on open source too given it shares PAL ...
		workarounds.force_sync1_access = true;
		// Avoids having to add workaround path to events as well, just fallback to plain barriers.
		workarounds.emulate_event_as_pipeline_barrier = true;
	}
#endif

	if (ext.supports_tooling_info && vkGetPhysicalDeviceToolPropertiesEXT)
	{
		uint32_t count = 0;
		vkGetPhysicalDeviceToolPropertiesEXT(gpu, &count, nullptr);
		Util::SmallVector<VkPhysicalDeviceToolPropertiesEXT> tool_props(count);
		for (auto &t : tool_props)
			t = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT };
		vkGetPhysicalDeviceToolPropertiesEXT(gpu, &count, tool_props.data());
		for (auto &t : tool_props)
		{
			LOGI("  Detected attached tool:\n");
			LOGI("    Name: %s\n", t.name);
			LOGI("    Description: %s\n", t.description);
			LOGI("    Version: %s\n", t.version);
			if ((t.purposes & VK_TOOL_PURPOSE_TRACING_BIT_EXT) != 0 &&
			    (t.purposes & VK_TOOL_PURPOSE_PROFILING_BIT) == 0)
			{
				LOGI("Detected non-profiling tracing tool, forcing host cached memory types for performance.\n");
				workarounds.force_host_cached = true;
			}

			if (!debug_marker_sensitive && (t.purposes & VK_TOOL_PURPOSE_DEBUG_MARKERS_BIT_EXT) != 0)
			{
				LOGI("Detected tool which cares about debug markers.\n");
				debug_marker_sensitive = true;
			}
		}
	}
}

void Device::set_context(const Context &context)
{
	ctx = &context;
	table = &context.get_device_table();

	register_thread_index(0);
	instance = context.get_instance();
	gpu = context.get_gpu();
	device = context.get_device();
	num_thread_indices = context.get_num_thread_indices();

	queue_info = context.get_queue_info();

	mem_props = context.get_mem_props();
	gpu_props = context.get_gpu_props();
	ext = context.get_enabled_device_features();
	system_handles = context.get_system_handles();

	init_workarounds();
	init_pipeline_cache();
	init_timeline_semaphores();
	init_frame_contexts(2); // By default, regular double buffer between CPU and GPU.

	managers.memory.init(this);
	managers.semaphore.init(this);
	managers.fence.init(this);
	managers.event.init(this);
	managers.vbo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	managers.ibo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	managers.ubo.init(this, 256 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment),
	                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	managers.ubo.set_spill_region_size(VULKAN_MAX_UBO_SIZE);
	managers.staging.init(this, 64 * 1024,
	                      std::max<VkDeviceSize>(gpu_props.limits.minStorageBufferOffsetAlignment,
	                                             std::max<VkDeviceSize>(16u, gpu_props.limits.optimalBufferCopyOffsetAlignment)),
	                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	managers.vbo.set_max_retained_blocks(256);
	managers.ibo.set_max_retained_blocks(256);
	managers.ubo.set_max_retained_blocks(64);
	managers.staging.set_max_retained_blocks(32);

	if (ext.supports_descriptor_buffer)
		managers.descriptor_buffer.init(this);

	init_stock_samplers();

	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
	{
		if (queue_info.family_indices[i] == VK_QUEUE_FAMILY_IGNORED)
			continue;

		bool alias_pool = false;
		for (int j = 0; j < i; j++)
		{
			if (queue_info.family_indices[i] == queue_info.family_indices[j])
			{
				alias_pool = true;
				break;
			}
		}

		if (!alias_pool)
			queue_data[i].performance_query_pool.init_device(this, queue_info.family_indices[i]);
	}

	if (system_handles.timeline_trace_file)
		init_calibrated_timestamps();

#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
	resource_manager.init();
#endif
}

void Device::begin_shader_caches()
{
	if (!ctx)
	{
		LOGE("No context. Forgot Device::set_context()?\n");
		return;
	}

#ifdef GRANITE_VULKAN_FOSSILIZE
	init_pipeline_state(ctx->get_feature_filter(), ctx->get_physical_device_features(),
	                    ctx->get_application_info());
#elif defined(GRANITE_VULKAN_SYSTEM_HANDLES)
	// Fossilize init will deal with init_shader_manager_cache()
	init_shader_manager_cache();
#endif
}

#ifndef GRANITE_VULKAN_FOSSILIZE
unsigned Device::query_initialization_progress(InitializationStage) const
{
	// If we don't have Fossilize, everything is considered done up front.
	return 100;
}

void Device::wait_shader_caches()
{
}
#endif

void Device::init_timeline_semaphores()
{
	if (!ext.vk12_features.timelineSemaphore)
		return;

	VkSemaphoreTypeCreateInfo type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
	VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	info.pNext = &type_info;
	type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	type_info.initialValue = 0;

	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
		if (table->vkCreateSemaphore(device, &info, nullptr, &queue_data[i].timeline_semaphore) != VK_SUCCESS)
			LOGE("Failed to create timeline semaphore.\n");
}

void Device::configure_default_geometry_samplers(float max_aniso, float lod_bias)
{
	init_stock_sampler(StockSampler::DefaultGeometryFilterClamp, max_aniso, lod_bias);
	init_stock_sampler(StockSampler::DefaultGeometryFilterWrap, max_aniso, lod_bias);
}

void Device::init_stock_sampler(StockSampler mode, float max_aniso, float lod_bias)
{
	SamplerCreateInfo info = {};
	info.max_lod = VK_LOD_CLAMP_NONE;
	info.max_anisotropy = 1.0f;

	switch (mode)
	{
	case StockSampler::NearestShadow:
	case StockSampler::LinearShadow:
		info.compare_enable = true;
		info.compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
		break;

	default:
		info.compare_enable = false;
		break;
	}

	switch (mode)
	{
	case StockSampler::TrilinearClamp:
	case StockSampler::TrilinearWrap:
	case StockSampler::DefaultGeometryFilterWrap:
	case StockSampler::DefaultGeometryFilterClamp:
		info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		break;

	default:
		info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		break;
	}

	switch (mode)
	{
	case StockSampler::DefaultGeometryFilterClamp:
	case StockSampler::DefaultGeometryFilterWrap:
	case StockSampler::LinearClamp:
	case StockSampler::LinearWrap:
	case StockSampler::TrilinearClamp:
	case StockSampler::TrilinearWrap:
	case StockSampler::LinearShadow:
		info.mag_filter = VK_FILTER_LINEAR;
		info.min_filter = VK_FILTER_LINEAR;
		break;

	default:
		info.mag_filter = VK_FILTER_NEAREST;
		info.min_filter = VK_FILTER_NEAREST;
		break;
	}

	switch (mode)
	{
	default:
	case StockSampler::DefaultGeometryFilterWrap:
	case StockSampler::LinearWrap:
	case StockSampler::NearestWrap:
	case StockSampler::TrilinearWrap:
		info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		break;

	case StockSampler::DefaultGeometryFilterClamp:
	case StockSampler::LinearClamp:
	case StockSampler::NearestClamp:
	case StockSampler::TrilinearClamp:
	case StockSampler::NearestShadow:
	case StockSampler::LinearShadow:
		info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		break;
	}

	switch (mode)
	{
	case StockSampler::DefaultGeometryFilterWrap:
	case StockSampler::DefaultGeometryFilterClamp:
		if (get_device_features().enabled_features.samplerAnisotropy)
		{
			info.anisotropy_enable = true;
			info.max_anisotropy = std::min(max_aniso, get_gpu_properties().limits.maxSamplerAnisotropy);
		}
		info.mip_lod_bias = lod_bias;
		break;

	default:
		break;
	}

	samplers[unsigned(mode)] = request_immutable_sampler(info, nullptr);
}

void Device::init_stock_samplers()
{
	for (unsigned i = 0; i < static_cast<unsigned>(StockSampler::Count); i++)
	{
		auto mode = static_cast<StockSampler>(i);
		init_stock_sampler(mode, 8.0f, 0.0f);
	}
}

static void request_block(Device &device, BufferBlock &block, VkDeviceSize size,
                          BufferPool &pool, std::vector<BufferBlock> &recycle)
{
	if (block.is_mapped())
		block.unmap(device);

	if (block.get_offset() == 0)
	{
		if (block.get_size() == pool.get_block_size())
			pool.recycle_block(block);
	}
	else
	{
		if (block.get_size() == pool.get_block_size())
			recycle.push_back(block);
	}

	if (size)
		block = pool.request_block(size);
	else
		block = {};
}

void Device::request_vertex_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_vertex_block_nolock(block, size);
}

void Device::request_vertex_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.vbo, frame().vbo_blocks);
}

void Device::request_index_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_index_block_nolock(block, size);
}

void Device::request_index_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.ibo, frame().ibo_blocks);
}

void Device::request_uniform_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_uniform_block_nolock(block, size);
}

void Device::request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.ubo, frame().ubo_blocks);
}

void Device::request_staging_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_staging_block_nolock(block, size);
}

void Device::request_staging_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.staging, frame().staging_blocks);
}

void Device::submit(CommandBufferHandle &cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	cmd->end_debug_channel();

	LOCK();
	submit_nolock(std::move(cmd), fence, semaphore_count, semaphores);
}

void Device::submit_and_sync_to_queues(CommandBufferHandle &cmd, uint32_t sync_to_queues)
{
	LOCK();

	auto type = cmd->get_command_buffer_type();
	auto physical_type = get_physical_queue_type(type);
	auto &data = queue_data[physical_type];

	// Resolve obvious cycles.
	uint32_t cycle_queues = queue_data[physical_type].has_incoming_queue_dependencies & sync_to_queues;
	Util::for_each_bit(cycle_queues, [&](unsigned bit) {
		flush_frame_nolock(QueueIndices(bit));
	});

	submit_nolock(std::move(cmd), nullptr, 0, nullptr);

	// Avoid self-sync which causes a loop.
	sync_to_queues &= ~(1u << physical_type);
	data.implicit_sync_to_queues |= sync_to_queues;
	Util::for_each_bit(sync_to_queues, [&](unsigned bit) {
		queue_data[QueueIndices(bit)].has_incoming_queue_dependencies |= 1u << physical_type;
	});

	// This is only used internally, and we should never introduce cycles on our own.
	// Verify that there is a flush path for the dependees which does not cause cycles.

	// Disable checks for now, it has bugs.
#if defined(VULKAN_DEBUG) && 0
	uint32_t executing_queues = sync_to_queues;
	uint32_t new_executing_queues = executing_queues;

	while (new_executing_queues != 0)
	{
		auto tmp_queues = new_executing_queues;
		new_executing_queues = 0;
		Util::for_each_bit(tmp_queues, [&](unsigned i) {
			if ((executing_queues & queue_data[i].has_incoming_queue_dependencies) != 0)
			{
				LOGE("Found cycle in internal staging commands.\n");
				abort();
			}
			else
			{
				new_executing_queues |= queue_data[i].has_incoming_queue_dependencies;
			}
		});

		executing_queues |= new_executing_queues;
	}
#endif
}

void Device::submit_discard_nolock(CommandBufferHandle &cmd)
{
#ifdef VULKAN_DEBUG
	auto type = cmd->get_command_buffer_type();
	auto &pool = frame().cmd_pools[get_physical_queue_type(type)][cmd->get_thread_index()];
	pool.signal_submitted(cmd->get_command_buffer());
#endif

	cmd->end();
	cmd.reset();
	decrement_frame_counter_nolock();
}

void Device::submit_discard(CommandBufferHandle &cmd)
{
	LOCK();
	submit_discard_nolock(cmd);
}

QueueIndices Device::get_physical_queue_type(CommandBuffer::Type queue_type) const
{
	// Enums match.
	return QueueIndices(queue_type);
}

void Device::submit_nolock(CommandBufferHandle cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	auto type = cmd->get_command_buffer_type();
	auto physical_type = get_physical_queue_type(type);
	auto &submissions = frame().submissions[physical_type];
#ifdef VULKAN_DEBUG
	auto &pool = frame().cmd_pools[physical_type][cmd->get_thread_index()];
	pool.signal_submitted(cmd->get_command_buffer());
#endif

	bool profiled_submit = cmd->has_profiling();

	if (profiled_submit)
	{
		LOGI("Submitting profiled command buffer, draining GPU.\n");
		Fence drain_fence;
		submit_empty_nolock(physical_type, &drain_fence, nullptr, -1);
		drain_fence->wait();
		drain_fence->set_internal_sync_object();
	}

	cmd->end();
	submissions.push_back(std::move(cmd));

	InternalFence signalled_fence;

	if (fence || semaphore_count)
	{
		submit_queue(physical_type, fence ? &signalled_fence : nullptr,
		             nullptr,
		             semaphore_count, semaphores,
		             profiled_submit ? 0 : -1);
	}

	if (fence)
	{
		VK_ASSERT(!*fence);
		if (signalled_fence.value)
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
		else
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
	}

	if (profiled_submit)
	{
		// Drain queue again and report results.
		LOGI("Submitted profiled command buffer, draining GPU and report ...\n");
		auto &query_pool = get_performance_query_pool(physical_type);
		Fence drain_fence;
		submit_empty_nolock(physical_type, &drain_fence, nullptr, fence || semaphore_count ? -1 : 0);
		drain_fence->wait();
		drain_fence->set_internal_sync_object();
		query_pool.report();
	}

	decrement_frame_counter_nolock();
}

void Device::submit_external(CommandBuffer::Type type)
{
	LOCK();
	auto &data = queue_data[get_physical_queue_type(type)];
	data.need_fence = true;
}

void Device::submit_empty(CommandBuffer::Type type, Fence *fence, SemaphoreHolder *semaphore)
{
	VK_ASSERT(!semaphore || !semaphore->is_proxy_timeline());
	LOCK();
	submit_empty_nolock(get_physical_queue_type(type), fence, semaphore, -1);
}

void Device::submit_empty_nolock(QueueIndices physical_type, Fence *fence,
                                 SemaphoreHolder *semaphore, int profiling_iteration)
{
	InternalFence signalled_fence = {};

	submit_queue(physical_type, fence ? &signalled_fence : nullptr, semaphore,
	             0, nullptr, profiling_iteration);

	if (fence)
	{
		if (signalled_fence.value)
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
		else
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
	}
}

void Device::submit_empty_inner(QueueIndices physical_type, InternalFence *fence,
                                SemaphoreHolder *external_semaphore,
                                unsigned semaphore_count, Semaphore *semaphores)
{
	auto &data = queue_data[physical_type];
	VkSemaphore timeline_semaphore = data.timeline_semaphore;
	uint64_t timeline_value = ++data.current_timeline;
	VkQueue queue = queue_info.queues[physical_type];
	frame().timeline_fences[physical_type] = data.current_timeline;

	// Add external wait semaphores.
	Helper::WaitSemaphores wait_semaphores;
	Helper::BatchComposer composer(get_device_features().supports_low_latency2_nv ? wsi.low_latency.present_id : 0);
	collect_wait_semaphores(data, wait_semaphores);
	composer.add_wait_submissions(wait_semaphores);

	for (auto consume : frame().consumed_semaphores)
	{
		composer.add_wait_semaphore(consume, VK_PIPELINE_STAGE_NONE);
		frame().recycled_semaphores.push_back(consume);
	}
	frame().consumed_semaphores.clear();

	emit_queue_signals(composer, external_semaphore,
	                   timeline_semaphore, timeline_value,
	                   fence, semaphore_count, semaphores);

	VkFence cleared_fence = fence && !ext.vk12_features.timelineSemaphore ?
	                        managers.fence.request_cleared_fence() :
	                        VK_NULL_HANDLE;
	if (fence)
		fence->fence = cleared_fence;

	auto start_ts = write_calibrated_timestamp_nolock();
	auto result = submit_batches(composer, queue, cleared_fence);
	auto end_ts = write_calibrated_timestamp_nolock();
	register_time_interval_nolock("CPU", std::move(start_ts), std::move(end_ts), "submit");

	emit_implicit_sync_to_queues(physical_type);

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit2 failed (code: %d).\n", int(result));

	if (!ext.vk12_features.timelineSemaphore)
		data.need_fence = true;
}

Fence Device::request_legacy_fence()
{
	VkFence fence = managers.fence.request_cleared_fence();
	return Fence(handle_pool.fences.allocate(this, fence));
}

void Device::collect_wait_semaphores(QueueData &data, Helper::WaitSemaphores &sem)
{
	VkSemaphoreSubmitInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };

	for (size_t i = 0, n = data.wait_semaphores.size(); i < n; i++)
	{
		auto &semaphore = data.wait_semaphores[i];
		auto vk_semaphore = semaphore->consume();
		if (semaphore->get_semaphore_type() == VK_SEMAPHORE_TYPE_TIMELINE)
		{
			info.semaphore = vk_semaphore;
			info.stageMask = data.wait_stages[i];
			info.value = semaphore->get_timeline_value();
			sem.timeline_waits.push_back(info);
		}
		else
		{
			if (semaphore->is_external_object_compatible())
				frame().destroyed_semaphores.push_back(vk_semaphore);
			else
				frame().recycled_semaphores.push_back(vk_semaphore);

			info.semaphore = vk_semaphore;
			info.stageMask = data.wait_stages[i];
			info.value = 0;
			sem.binary_waits.push_back(info);
		}
	}

	data.wait_stages.clear();
	data.wait_semaphores.clear();
}

Helper::BatchComposer::BatchComposer(uint64_t present_id_nv_)
	: present_id_nv(present_id_nv_)
{
	submits.emplace_back();
}

void Helper::BatchComposer::begin_batch()
{
	if (!waits[submit_index].empty() || !cmds[submit_index].empty() || !signals[submit_index].empty())
	{
		submit_index = submits.size();
		submits.emplace_back();
		VK_ASSERT(submits.size() <= MaxSubmissions);
	}
}

void Helper::BatchComposer::add_wait_submissions(WaitSemaphores &sem)
{
	auto &w = waits[submit_index];

	if (!sem.binary_waits.empty())
		w.insert(w.end(), sem.binary_waits.begin(), sem.binary_waits.end());

	if (!sem.timeline_waits.empty())
		w.insert(w.end(), sem.timeline_waits.begin(), sem.timeline_waits.end());
}

SmallVector<VkSubmitInfo2, Helper::BatchComposer::MaxSubmissions> &
Helper::BatchComposer::bake(int profiling_iteration)
{
	if (present_id_nv)
		present_ids_nv.resize(submits.size());

	for (size_t i = 0, n = submits.size(); i < n; i++)
	{
		auto &submit = submits[i];

		submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submit.commandBufferInfoCount = uint32_t(cmds[i].size());
		submit.pCommandBufferInfos = cmds[i].data();
		submit.signalSemaphoreInfoCount = uint32_t(signals[i].size());
		submit.pSignalSemaphoreInfos = signals[i].data();
		submit.waitSemaphoreInfoCount = uint32_t(waits[i].size());
		submit.pWaitSemaphoreInfos = waits[i].data();

		if (present_id_nv)
		{
			present_ids_nv[i].sType = VK_STRUCTURE_TYPE_LATENCY_SUBMISSION_PRESENT_ID_NV;
			present_ids_nv[i].presentID = present_id_nv;
			present_ids_nv[i].pNext = submit.pNext;
			submit.pNext = &present_ids_nv[i];
		}

		if (profiling_iteration >= 0)
		{
			profiling_infos[i] = { VK_STRUCTURE_TYPE_PERFORMANCE_QUERY_SUBMIT_INFO_KHR };
			profiling_infos[i].counterPassIndex = uint32_t(profiling_iteration);
			profiling_infos[i].pNext = submit.pNext;
			submit.pNext = &profiling_infos[i];
		}
	}

	// Compact the submission array to avoid empty submissions.
	size_t submit_count = 0;
	for (size_t i = 0, n = submits.size(); i < n; i++)
	{
		if (submits[i].waitSemaphoreInfoCount || submits[i].signalSemaphoreInfoCount || submits[i].commandBufferInfoCount)
		{
			if (i != submit_count)
				submits[submit_count] = submits[i];
			submit_count++;
		}
	}

	submits.resize(submit_count);
	return submits;
}

void Helper::BatchComposer::add_command_buffer(VkCommandBuffer cmd)
{
	if (!signals[submit_index].empty())
		begin_batch();

	VkCommandBufferSubmitInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	info.commandBuffer = cmd;
	cmds[submit_index].push_back(info);
}

void Helper::BatchComposer::add_signal_semaphore(VkSemaphore sem, VkPipelineStageFlags2 stages, uint64_t timeline)
{
	VkSemaphoreSubmitInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	info.semaphore = sem;
	info.stageMask = stages;
	info.value = timeline;
	signals[submit_index].push_back(info);
}

void Helper::BatchComposer::add_wait_semaphore(SemaphoreHolder &sem, VkPipelineStageFlags2 stage)
{
	if (!cmds[submit_index].empty() || !signals[submit_index].empty())
		begin_batch();

	bool is_timeline = sem.get_semaphore_type() == VK_SEMAPHORE_TYPE_TIMELINE;

	VkSemaphoreSubmitInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	info.semaphore = sem.get_semaphore();
	info.stageMask = stage;
	info.value = is_timeline ? sem.get_timeline_value() : 0;
	waits[submit_index].push_back(info);
}

void Helper::BatchComposer::add_wait_semaphore(VkSemaphore sem, VkPipelineStageFlags2 stage)
{
	if (!cmds[submit_index].empty() || !signals[submit_index].empty())
		begin_batch();

	VkSemaphoreSubmitInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	info.semaphore = sem;
	info.stageMask = stage;
	info.value = 0;
	waits[submit_index].push_back(info);
}

void Device::emit_implicit_sync_to_queues(QueueIndices physical_type)
{
	auto &data = queue_data[physical_type];
	auto sync_to_queues = data.implicit_sync_to_queues;
	// Clear this early to avoid infinite recursion.
	data.implicit_sync_to_queues = 0;

	if (ext.vk12_features.timelineSemaphore)
	{
		Util::for_each_bit(sync_to_queues, [&](unsigned bit)
		{
			auto queue_index = QueueIndices(bit);
			auto sem = Semaphore(
					handle_pool.semaphores.allocate(this, data.current_timeline, data.timeline_semaphore, false));
			sem->signal_external();

			// Ensure that all pending command buffers observe the wait since we have deferred adding the signal.
			auto &dependee = queue_data[queue_index];
			dependee.wait_semaphores.push_back(std::move(sem));
			dependee.wait_stages.push_back(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
			dependee.has_incoming_queue_dependencies &= ~(1u << physical_type);
		});
	}
	else
	{
		Util::for_each_bit(sync_to_queues, [&](unsigned bit)
		{
			auto sem = request_legacy_semaphore();
			submit_empty_inner(physical_type, nullptr, sem.get(), 0, nullptr);

			auto queue_index = QueueIndices(bit);

			// Ensure that all pending command buffers observe the wait since we have deferred adding the signal.
			queue_data[queue_index].wait_semaphores.push_back(std::move(sem));
			queue_data[queue_index].wait_stages.push_back(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
			queue_data[queue_index].has_incoming_queue_dependencies &= ~(1u << physical_type);
		});
	}
}

void Device::emit_queue_signals(Helper::BatchComposer &composer,
                                SemaphoreHolder *external_semaphore,
                                VkSemaphore sem, uint64_t timeline, InternalFence *fence,
                                unsigned semaphore_count, Semaphore *semaphores)
{
	if (external_semaphore)
	{
		VK_ASSERT(!external_semaphore->is_signalled());
		VK_ASSERT(!external_semaphore->is_proxy_timeline());
		VK_ASSERT(external_semaphore->get_semaphore());
		external_semaphore->signal_external();
		composer.add_signal_semaphore(external_semaphore->get_semaphore(),
		                              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		                              external_semaphore->get_semaphore_type() == VK_SEMAPHORE_TYPE_TIMELINE ?
		                              external_semaphore->get_timeline_value() : 0);

		// Make sure we observe that the external semaphore is signalled before fences are signalled.
		composer.begin_batch();
	}

	// Add external signal semaphores.
	if (ext.vk12_features.timelineSemaphore)
	{
		// Signal once and distribute the timeline value to all.
		composer.add_signal_semaphore(sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timeline);

		if (fence)
		{
			fence->timeline = sem;
			fence->value = timeline;
			fence->fence = VK_NULL_HANDLE;
		}

		for (unsigned i = 0; i < semaphore_count; i++)
		{
			VK_ASSERT(!semaphores[i]);
			semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, timeline, sem, false));
			semaphores[i]->signal_external();
		}
	}
	else
	{
		if (fence)
		{
			fence->timeline = VK_NULL_HANDLE;
			fence->value = 0;
		}

		for (unsigned i = 0; i < semaphore_count; i++)
		{
			VkSemaphore cleared_semaphore = managers.semaphore.request_cleared_semaphore();
			composer.add_signal_semaphore(cleared_semaphore, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0);
			VK_ASSERT(!semaphores[i]);
			semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true, true));
		}
	}
}

VkResult Device::queue_submit(VkQueue queue, uint32_t count, const VkSubmitInfo2 *submits, VkFence fence)
{
	if (ext.vk13_features.synchronization2)
	{
		return table->vkQueueSubmit2(queue, count, submits, fence);
	}
	else
	{
		for (uint32_t submit_index = 0; submit_index < count; submit_index++)
		{
			VkTimelineSemaphoreSubmitInfo timeline = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
			const auto &submit = submits[submit_index];
			VkSubmitInfo sub = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
			bool need_timeline = false;

			Util::SmallVector<VkPipelineStageFlags> wait_stages;
			Util::SmallVector<uint64_t> signal_values;
			Util::SmallVector<uint64_t> wait_values;
			Util::SmallVector<VkSemaphore> signals;
			Util::SmallVector<VkCommandBuffer> cmd;
			Util::SmallVector<VkSemaphore> waits;

			for (uint32_t i = 0; i < submit.commandBufferInfoCount; i++)
				cmd.push_back(submit.pCommandBufferInfos[i].commandBuffer);

			for (uint32_t i = 0; i < submit.waitSemaphoreInfoCount; i++)
			{
				waits.push_back(submit.pWaitSemaphoreInfos[i].semaphore);
				wait_stages.push_back(convert_vk_dst_stage2(submit.pWaitSemaphoreInfos[i].stageMask));
				wait_values.push_back(submit.pWaitSemaphoreInfos[i].value);
				if (wait_values.back() != 0)
					need_timeline = true;
			}

			for (uint32_t i = 0; i < submit.signalSemaphoreInfoCount; i++)
			{
				signals.push_back(submit.pSignalSemaphoreInfos[i].semaphore);
				signal_values.push_back(submit.pSignalSemaphoreInfos[i].value);
				if (signal_values.back() != 0)
					need_timeline = true;
			}

			sub.commandBufferCount = uint32_t(cmd.size());
			sub.pCommandBuffers = cmd.data();
			sub.signalSemaphoreCount = uint32_t(signals.size());
			sub.pSignalSemaphores = signals.data();
			sub.waitSemaphoreCount = uint32_t(waits.size());
			sub.pWaitSemaphores = waits.data();
			sub.pWaitDstStageMask = wait_stages.data();

			sub.pNext = submit.pNext;
			if (need_timeline)
			{
				timeline.pNext = sub.pNext;
				sub.pNext = &timeline;

				timeline.signalSemaphoreValueCount = uint32_t(signal_values.size());
				timeline.pSignalSemaphoreValues = signal_values.data();
				timeline.waitSemaphoreValueCount = uint32_t(wait_values.size());
				timeline.pWaitSemaphoreValues = wait_values.data();
			}

			auto result = table->vkQueueSubmit(queue, 1, &sub, submit_index + 1 == count ? fence : VK_NULL_HANDLE);
			if (result != VK_SUCCESS)
				return result;
		}

		if (count == 0 && fence)
		{
			auto result = table->vkQueueSubmit(queue, 0, nullptr, fence);
			if (result != VK_SUCCESS)
				return result;
		}

		return VK_SUCCESS;
	}
}

VkResult Device::submit_batches(Helper::BatchComposer &composer, VkQueue queue, VkFence fence, int profiling_iteration)
{
	auto &submits = composer.bake(profiling_iteration);
	if (queue_lock_callback)
		queue_lock_callback();

	VkResult result = queue_submit(queue, uint32_t(submits.size()), submits.data(), fence);

	if (ImplementationQuirks::get().queue_wait_on_submission)
		table->vkQueueWaitIdle(queue);
	if (queue_unlock_callback)
		queue_unlock_callback();

	return result;
}

void Device::submit_queue(QueueIndices physical_type, InternalFence *fence,
                          SemaphoreHolder *external_semaphore,
                          unsigned semaphore_count, Semaphore *semaphores, int profiling_iteration)
{
	auto &data = queue_data[physical_type];
	Util::for_each_bit(data.has_incoming_queue_dependencies, [&](unsigned bits) {
		VK_ASSERT(physical_type != bits);
		submit_queue(QueueIndices(bits), nullptr);
	});
	VK_ASSERT(data.has_incoming_queue_dependencies == 0);

	auto &submissions = frame().submissions[physical_type];

	if (submissions.empty())
	{
		if (fence || semaphore_count || external_semaphore || data.implicit_sync_to_queues || !data.wait_semaphores.empty())
			submit_empty_inner(physical_type, fence, external_semaphore, semaphore_count, semaphores);
		return;
	}

	if (get_device_features().supports_low_latency2_nv &&
	    wsi.low_latency.need_submit_begin_marker &&
	    wsi.low_latency.present_id &&
	    wsi.low_latency.swapchain)
	{
		VkSetLatencyMarkerInfoNV marker_info = { VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV };
		marker_info.presentID = wsi.low_latency.present_id;
		table->vkSetLatencyMarkerNV(device, wsi.low_latency.swapchain, &marker_info);
		wsi.low_latency.need_submit_begin_marker = false;
	}

	VkSemaphore timeline_semaphore = data.timeline_semaphore;
	uint64_t timeline_value = ++data.current_timeline;

	VkQueue queue = queue_info.queues[physical_type];
	frame().timeline_fences[physical_type] = data.current_timeline;

	Helper::BatchComposer composer(get_device_features().supports_low_latency2_nv ? wsi.low_latency.present_id : 0);
	Helper::WaitSemaphores wait_semaphores;
	collect_wait_semaphores(data, wait_semaphores);

	composer.add_wait_submissions(wait_semaphores);

	// Find first command buffer which uses WSI, we'll need to emit WSI acquire wait before the first command buffer
	// that uses WSI image.

	for (size_t i = 0, submissions_size = submissions.size(); i < submissions_size; i++)
	{
		auto &cmd = submissions[i];
		VkPipelineStageFlags2 wsi_stages = cmd->swapchain_touched_in_stages();

		if (wsi_stages != 0 && !wsi.consumed)
		{
			if (!can_touch_swapchain_in_command_buffer(physical_type))
				LOGE("Touched swapchain in unsupported command buffer type %u.\n", unsigned(physical_type));

			if (wsi.acquire && wsi.acquire->get_semaphore() != VK_NULL_HANDLE)
			{
				VK_ASSERT(wsi.acquire->is_signalled());
				composer.add_wait_semaphore(*wsi.acquire, wsi_stages);
				if (wsi.acquire->get_semaphore_type() == VK_SEMAPHORE_TYPE_BINARY)
				{
					if (wsi.acquire->is_external_object_compatible())
						frame().destroyed_semaphores.push_back(wsi.acquire->get_semaphore());
					else
						frame().recycled_semaphores.push_back(wsi.acquire->get_semaphore());
				}
				wsi.acquire->consume();
				wsi.acquire.reset();
			}

			composer.add_command_buffer(cmd->get_command_buffer());

			VkSemaphore release = managers.semaphore.request_cleared_semaphore();
			wsi.release = Semaphore(handle_pool.semaphores.allocate(this, release, true, true));
			wsi.release->set_internal_sync_object();
			composer.add_signal_semaphore(release, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0);
			wsi.present_queue = queue;
			wsi.present_queue_type = cmd->get_command_buffer_type();
			wsi.consumed = true;
		}
		else
		{
			// After we have consumed WSI, we cannot keep using it, since we
			// already signalled the semaphore.
			VK_ASSERT(wsi_stages == 0);
			composer.add_command_buffer(cmd->get_command_buffer());
		}
	}

	VkFence cleared_fence = fence && !ext.vk12_features.timelineSemaphore ?
	                        managers.fence.request_cleared_fence() :
	                        VK_NULL_HANDLE;

	if (fence)
		fence->fence = cleared_fence;

	for (auto consume : frame().consumed_semaphores)
	{
		composer.add_wait_semaphore(consume, VK_PIPELINE_STAGE_NONE);
		frame().recycled_semaphores.push_back(consume);
	}
	frame().consumed_semaphores.clear();

	emit_queue_signals(composer, external_semaphore, timeline_semaphore, timeline_value,
	                   fence, semaphore_count, semaphores);

	auto start_ts = write_calibrated_timestamp_nolock();
	auto result = submit_batches(composer, queue, cleared_fence, profiling_iteration);
	auto end_ts = write_calibrated_timestamp_nolock();
	register_time_interval_nolock("CPU", std::move(start_ts), std::move(end_ts), "submit");

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit2 failed (code: %d).\n", int(result));
	submissions.clear();

	emit_implicit_sync_to_queues(physical_type);

	if (!ext.vk12_features.timelineSemaphore)
		data.need_fence = true;
}

void Device::flush_frame_nolock(QueueIndices physical_type)
{
	if (queue_info.queues[physical_type] != VK_NULL_HANDLE)
		submit_queue(physical_type, nullptr);
}

void Device::end_frame_context()
{
	DRAIN_FRAME_LOCK();
	end_frame_nolock();
}

void Device::end_frame_nolock()
{
	// Flushing one queue may require flushes on other queues.
	// Make sure everything is resolved before we check for fences.
	// This is mostly unnecessary with timeline semaphores.
	flush_frame_nolock();

	// Make sure we have a fence which covers all submissions in the frame.
	for (auto &i : queue_flush_order)
	{
		if (queue_data[i].need_fence ||
		    !frame().submissions[i].empty() ||
		    !frame().consumed_semaphores.empty())
		{
			InternalFence fence = {};
			submit_queue(i, &fence);
			if (fence.fence != VK_NULL_HANDLE)
				frame().wait_and_recycle_fences.push_back(fence.fence);
			queue_data[i].need_fence = false;

			VK_ASSERT(queue_data[i].wait_semaphores.empty());
		}
	}
}

void Device::flush_frame()
{
	LOCK();
	flush_frame_nolock();
}

void Device::flush_frame_nolock()
{
	for (auto &i : queue_flush_order)
		flush_frame_nolock(i);
}

PerformanceQueryPool &Device::get_performance_query_pool(QueueIndices physical_index)
{
	for (int i = 0; i < physical_index; i++)
		if (queue_info.family_indices[i] == queue_info.family_indices[physical_index])
			return queue_data[i].performance_query_pool;
	return queue_data[physical_index].performance_query_pool;
}

CommandBufferHandle Device::request_command_buffer(CommandBuffer::Type type)
{
	return request_command_buffer_for_thread(get_thread_index(), type);
}

CommandBufferHandle Device::request_command_buffer_for_thread(unsigned thread_index, CommandBuffer::Type type)
{
	LOCK();
	return request_command_buffer_nolock(thread_index, type, false);
}

CommandBufferHandle Device::request_profiled_command_buffer(CommandBuffer::Type type)
{
	return request_profiled_command_buffer_for_thread(get_thread_index(), type);
}

CommandBufferHandle Device::request_profiled_command_buffer_for_thread(unsigned thread_index,
                                                                       CommandBuffer::Type type)
{
	LOCK();
	return request_command_buffer_nolock(thread_index, type, true);
}

CommandBufferHandle Device::request_command_buffer_nolock(unsigned thread_index, CommandBuffer::Type type, bool profiled)
{
	auto physical_type = get_physical_queue_type(type);
	auto &pool = frame().cmd_pools[physical_type][thread_index];
	auto cmd = pool.request_command_buffer();

	if (profiled && !ext.performance_query_features.performanceCounterQueryPools)
	{
		LOGW("Profiling is not supported on this device.\n");
		profiled = false;
	}

	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	table->vkBeginCommandBuffer(cmd, &info);
	add_frame_counter_nolock();
	CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, legacy_pipeline_cache, type));
	handle->set_thread_index(thread_index);

	if (profiled)
	{
		auto &query_pool = get_performance_query_pool(physical_type);
		handle->enable_profiling();
		query_pool.begin_command_buffer(handle->get_command_buffer());
	}

	return handle;
}

void Device::submit_secondary(CommandBuffer &primary, CommandBuffer &secondary)
{
	{
		LOCK();
		secondary.end();
		decrement_frame_counter_nolock();

#ifdef VULKAN_DEBUG
		auto &pool = frame().cmd_pools[get_physical_queue_type(secondary.get_command_buffer_type())][secondary.get_thread_index()];
		pool.signal_submitted(secondary.get_command_buffer());
#endif
	}

	VkCommandBuffer secondary_cmd = secondary.get_command_buffer();
	table->vkCmdExecuteCommands(primary.get_command_buffer(), 1, &secondary_cmd);
}

CommandBufferHandle Device::request_secondary_command_buffer_for_thread(unsigned thread_index,
                                                                        const Framebuffer *framebuffer,
                                                                        unsigned subpass,
                                                                        CommandBuffer::Type type)
{
	LOCK();

	auto &pool = frame().cmd_pools[get_physical_queue_type(type)][thread_index];
	auto cmd = pool.request_secondary_command_buffer();
	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VkCommandBufferInheritanceInfo inherit = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };

	inherit.framebuffer = VK_NULL_HANDLE;
	inherit.renderPass = framebuffer->get_compatible_render_pass().get_render_pass();
	inherit.subpass = subpass;
	info.pInheritanceInfo = &inherit;
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

	table->vkBeginCommandBuffer(cmd, &info);
	add_frame_counter_nolock();
	CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, legacy_pipeline_cache, type));
	handle->set_thread_index(thread_index);
	handle->set_is_secondary();
	return handle;
}

void Device::set_acquire_semaphore(unsigned index, Semaphore acquire)
{
	wsi.acquire = std::move(acquire);
	wsi.index = index;
	wsi.consumed = false;

	if (wsi.acquire)
	{
		wsi.acquire->set_internal_sync_object();
		VK_ASSERT(wsi.acquire->is_signalled());
	}
}

void Device::set_present_id(VkSwapchainKHR swapchain, uint64_t present_id)
{
	if (wsi.low_latency.present_id != present_id)
		wsi.low_latency.need_submit_begin_marker = true;
	wsi.low_latency.swapchain = swapchain;
	wsi.low_latency.present_id = present_id;
}

Semaphore Device::consume_release_semaphore()
{
	auto ret = std::move(wsi.release);
	wsi.release.reset();
	return ret;
}

VkQueue Device::get_current_present_queue() const
{
	VK_ASSERT(wsi.present_queue);
	return wsi.present_queue;
}

CommandBuffer::Type Device::get_current_present_queue_type() const
{
	VK_ASSERT(wsi.present_queue);
	return wsi.present_queue_type;
}

const Sampler &Device::get_stock_sampler(StockSampler sampler) const
{
	return samplers[static_cast<unsigned>(sampler)]->get_sampler();
}

bool Device::swapchain_touched() const
{
	return wsi.consumed;
}

Device::~Device()
{
	wsi.acquire.reset();
	wsi.release.reset();
	wsi.swapchain.clear();
	managers.descriptor_buffer.teardown();

	wait_idle();

	managers.timestamps.log_simple();

#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
	flush_shader_manager_cache();
#endif

#ifdef GRANITE_VULKAN_FOSSILIZE
	flush_pipeline_state();
#endif

	if (legacy_pipeline_cache != VK_NULL_HANDLE || ext.pipeline_binary_features.pipelineBinaries)
		flush_pipeline_cache();
	table->vkDestroyPipelineCache(device, legacy_pipeline_cache, nullptr);

	framebuffer_allocator.clear();
	transient_allocator.clear();

	deinit_timeline_semaphores();
}

void Device::deinit_timeline_semaphores()
{
	for (auto &data : queue_data)
	{
		if (data.timeline_semaphore != VK_NULL_HANDLE)
			table->vkDestroySemaphore(device, data.timeline_semaphore, nullptr);
		data.timeline_semaphore = VK_NULL_HANDLE;
	}

	// Make sure we don't accidentally try to wait for these after we destroy the semaphores.
	for (auto &frame : per_frame)
	{
		for (auto &fence : frame->timeline_fences)
			fence = 0;
		for (auto &timeline : frame->timeline_semaphores)
			timeline = VK_NULL_HANDLE;
	}
}

void Device::init_frame_contexts(unsigned count)
{
	DRAIN_FRAME_LOCK();
	wait_idle_nolock();

	// Clear out caches which might contain stale data from now on.
	framebuffer_allocator.clear();
	transient_allocator.clear();
	per_frame.clear();

	for (unsigned i = 0; i < count; i++)
	{
		auto frame = std::unique_ptr<PerFrame>(new PerFrame(this, i));
		per_frame.emplace_back(std::move(frame));
	}
}

void Device::init_external_swapchain(const std::vector<ImageHandle> &swapchain_images)
{
	DRAIN_FRAME_LOCK();
	wsi.swapchain.clear();
	wait_idle_nolock();

	wsi.index = 0;
	wsi.consumed = false;
	for (auto &image : swapchain_images)
	{
		wsi.swapchain.push_back(image);
		if (image)
		{
			wsi.swapchain.back()->set_internal_sync_object();
			wsi.swapchain.back()->get_view().set_internal_sync_object();
		}
	}
}

bool Device::can_touch_swapchain_in_command_buffer(QueueIndices physical_type) const
{
	// If 0, we have virtual swap chain, so anything goes.
	if (!wsi.queue_family_support_mask)
		return true;

	return (wsi.queue_family_support_mask & (1u << queue_info.family_indices[physical_type])) != 0;
}

bool Device::can_touch_swapchain_in_command_buffer(CommandBuffer::Type type) const
{
	return can_touch_swapchain_in_command_buffer(get_physical_queue_type(type));
}

void Device::set_swapchain_queue_family_support(uint32_t queue_family_support)
{
	wsi.queue_family_support_mask = queue_family_support;
}

ImageHandle Device::wrap_image(const ImageCreateInfo &info, VkImage image)
{
	auto img = ImageHandle(handle_pool.images.allocate(
			this, image, VK_NULL_HANDLE,
			DeviceAllocation{}, info, VK_IMAGE_VIEW_TYPE_MAX_ENUM));
	img->disown_image();
	return img;
}

void Device::init_swapchain(const std::vector<VkImage> &swapchain_images, unsigned width, unsigned height, VkFormat format,
                            VkSurfaceTransformFlagBitsKHR transform, VkImageUsageFlags usage, VkImageLayout layout)
{
	DRAIN_FRAME_LOCK();
	wsi.swapchain.clear();

	auto info = ImageCreateInfo::render_target(width, height, format);
	info.usage = usage;

	wsi.index = 0;
	wsi.consumed = false;
	for (auto &image : swapchain_images)
	{
		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.format = format;
		view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.layerCount = 1;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;

		VkImageView image_view;
		if (table->vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
			LOGE("Failed to create view for backbuffer.");

		auto backbuffer = ImageHandle(handle_pool.images.allocate(this, image, image_view, DeviceAllocation{}, info, VK_IMAGE_VIEW_TYPE_2D));
		backbuffer->set_internal_sync_object();
		backbuffer->disown_image();
		backbuffer->get_view().set_internal_sync_object();
		backbuffer->set_surface_transform(transform);
		wsi.swapchain.push_back(backbuffer);
		set_name(*backbuffer, "backbuffer");
		backbuffer->set_swapchain_layout(layout);
	}
}

Device::PerFrame::PerFrame(Device *device_, unsigned frame_index_)
    : device(*device_)
    , frame_index(frame_index_)
    , table(device_->get_device_table())
    , managers(device_->managers)
    , query_pool(device_)
{
	unsigned count = device_->num_thread_indices;
	for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
	{
		timeline_semaphores[i] = device.queue_data[i].timeline_semaphore;
		cmd_pools[i].reserve(count);
		for (unsigned j = 0; j < count; j++)
			cmd_pools[i].emplace_back(device_, device_->queue_info.family_indices[i]);
	}
}

void Device::free_memory_nolock(const DeviceAllocation &alloc)
{
	frame().allocations.push_back(alloc);
}

#ifdef VULKAN_DEBUG

template <typename T, typename U>
static inline bool exists(const T &container, const U &value)
{
	return find(begin(container), end(container), value) != end(container);
}

#endif

void Device::reset_fence(VkFence fence, bool observed_wait)
{
	LOCK();
	reset_fence_nolock(fence, observed_wait);
}

void Device::destroy_buffer(VkBuffer buffer)
{
	LOCK();
	destroy_buffer_nolock(buffer);
}

void Device::destroy_indirect_execution_set(VkIndirectExecutionSetEXT exec_set)
{
	LOCK();
	destroy_indirect_execution_set_nolock(exec_set);
}

void Device::destroy_descriptor_pool(VkDescriptorPool desc_pool)
{
	LOCK();
	destroy_descriptor_pool_nolock(desc_pool);
}

void Device::destroy_buffer_view(VkBufferView view)
{
	LOCK();
	destroy_buffer_view_nolock(view);
}

void Device::destroy_event(VkEvent event)
{
	LOCK();
	destroy_event_nolock(event);
}

void Device::destroy_framebuffer(VkFramebuffer framebuffer)
{
	LOCK();
	destroy_framebuffer_nolock(framebuffer);
}

void Device::destroy_image(VkImage image)
{
	LOCK();
	destroy_image_nolock(image);
}

void Device::destroy_semaphore(VkSemaphore semaphore)
{
	LOCK();
	destroy_semaphore_nolock(semaphore);
}

void Device::consume_semaphore(VkSemaphore semaphore)
{
	LOCK();
	consume_semaphore_nolock(semaphore);
}

void Device::recycle_semaphore(VkSemaphore semaphore)
{
	LOCK();
	recycle_semaphore_nolock(semaphore);
}

void Device::free_memory(const DeviceAllocation &alloc)
{
	LOCK();
	free_memory_nolock(alloc);
}

void Device::destroy_sampler(VkSampler sampler)
{
	LOCK();
	destroy_sampler_nolock(sampler);
}

void Device::destroy_image_view(VkImageView view)
{
	LOCK();
	destroy_image_view_nolock(view);
}

void Device::free_descriptor_buffer_allocation(const DescriptorBufferAllocation &alloc)
{
	LOCK();
	free_descriptor_buffer_allocation_nolock(alloc);
}

void Device::free_cached_descriptor_payload(const CachedDescriptorPayload &payload)
{
	LOCK();
	free_cached_descriptor_payload_nolock(payload);
}

void Device::destroy_image_view_nolock(VkImageView view)
{
	VK_ASSERT(!exists(frame().destroyed_image_views, view));
	frame().destroyed_image_views.push_back(view);
}

void Device::destroy_buffer_view_nolock(VkBufferView view)
{
	VK_ASSERT(!exists(frame().destroyed_buffer_views, view));
	frame().destroyed_buffer_views.push_back(view);
}

void Device::destroy_semaphore_nolock(VkSemaphore semaphore)
{
	VK_ASSERT(!exists(frame().destroyed_semaphores, semaphore));
	frame().destroyed_semaphores.push_back(semaphore);
}

void Device::consume_semaphore_nolock(VkSemaphore semaphore)
{
	VK_ASSERT(!exists(frame().consumed_semaphores, semaphore));
	frame().consumed_semaphores.push_back(semaphore);
}

void Device::recycle_semaphore_nolock(VkSemaphore semaphore)
{
	VK_ASSERT(!exists(frame().recycled_semaphores, semaphore));
	frame().recycled_semaphores.push_back(semaphore);
}

void Device::destroy_event_nolock(VkEvent event)
{
	VK_ASSERT(!exists(frame().recycled_events, event));
	frame().recycled_events.push_back(event);
}

void Device::reset_fence_nolock(VkFence fence, bool observed_wait)
{
	if (observed_wait)
	{
		table->vkResetFences(device, 1, &fence);
		managers.fence.recycle_fence(fence);
	}
	else
		frame().wait_and_recycle_fences.push_back(fence);
}

void Device::free_descriptor_buffer_allocation_nolock(const DescriptorBufferAllocation &alloc)
{
	frame().descriptor_buffer_allocs.push_back(alloc);
}

void Device::free_cached_descriptor_payload_nolock(const CachedDescriptorPayload &payload)
{
	frame().cached_descriptor_payloads.push_back(payload);
}

PipelineEvent Device::request_pipeline_event()
{
	return PipelineEvent(handle_pool.events.allocate(this, managers.event.request_cleared_event()));
}

void Device::destroy_image_nolock(VkImage image)
{
	VK_ASSERT(!exists(frame().destroyed_images, image));
	frame().destroyed_images.push_back(image);
}

void Device::destroy_buffer_nolock(VkBuffer buffer)
{
	VK_ASSERT(!exists(frame().destroyed_buffers, buffer));
	frame().destroyed_buffers.push_back(buffer);
}

void Device::destroy_indirect_execution_set_nolock(VkIndirectExecutionSetEXT exec_set)
{
	VK_ASSERT(!exists(frame().destroyed_execution_sets, exec_set));
	frame().destroyed_execution_sets.push_back(exec_set);
}

void Device::destroy_descriptor_pool_nolock(VkDescriptorPool desc_pool)
{
	VK_ASSERT(!exists(frame().destroyed_descriptor_pools, desc_pool));
	frame().destroyed_descriptor_pools.push_back(desc_pool);
}

void Device::destroy_sampler_nolock(VkSampler sampler)
{
	VK_ASSERT(!exists(frame().destroyed_samplers, sampler));
	frame().destroyed_samplers.push_back(sampler);
}

void Device::destroy_framebuffer_nolock(VkFramebuffer framebuffer)
{
	VK_ASSERT(!exists(frame().destroyed_framebuffers, framebuffer));
	frame().destroyed_framebuffers.push_back(framebuffer);
}

void Device::wait_idle()
{
	DRAIN_FRAME_LOCK();
	wait_idle_nolock();
}

void Device::wait_idle_nolock()
{
	if (!per_frame.empty())
		end_frame_nolock();

	if (device != VK_NULL_HANDLE)
	{
		if (queue_lock_callback)
			queue_lock_callback();
		auto result = table->vkDeviceWaitIdle(device);
		if (result != VK_SUCCESS)
			LOGE("vkDeviceWaitIdle failed with code: %d\n", result);
		if (queue_unlock_callback)
			queue_unlock_callback();
	}

	// Free memory for buffer pools.
	managers.vbo.reset();
	managers.ubo.reset();
	managers.ibo.reset();
	managers.staging.reset();
	for (auto &frame : per_frame)
	{
		frame->vbo_blocks.clear();
		frame->ibo_blocks.clear();
		frame->ubo_blocks.clear();
		frame->staging_blocks.clear();
	}

	framebuffer_allocator.clear();
	transient_allocator.clear();

	if (!ext.supports_descriptor_buffer)
	{
		for (auto &allocator: descriptor_set_allocators.get_read_only())
			allocator.clear();
		for (auto &allocator: descriptor_set_allocators.get_read_write())
			allocator.clear();
	}

	for (auto &frame : per_frame)
	{
		frame->begin();
		frame->trim_command_pools();
	}

	{
		LOCK_MEMORY();
		managers.memory.garbage_collect();
	}
}

void Device::promote_read_write_caches_to_read_only()
{
	// Components which could potentially call into these must hold global reader locks.
	// - A CommandBuffer holds a read lock for its lifetime.
	// - Fossilize replay in the background also holds lock.
	if (lock.read_only_cache.try_lock_write())
	{
		pipeline_layouts.move_to_read_only();
		descriptor_set_allocators.move_to_read_only();
		shaders.move_to_read_only();
		programs.move_to_read_only();
		for (auto &program : programs.get_read_only())
			program.promote_read_write_to_read_only();
		render_passes.move_to_read_only();
		immutable_samplers.move_to_read_only();
		immutable_ycbcr_conversions.move_to_read_only();
#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
		shader_manager.promote_read_write_caches_to_read_only();
#endif
		lock.read_only_cache.unlock_write();
	}
}

void Device::set_enable_async_thread_frame_context(bool enable)
{
	LOCK();
	lock.async_frame_context = enable;
}

void Device::next_frame_context_in_async_thread()
{
	bool do_next_frame_context;
	{
		LOCK();
		do_next_frame_context = lock.async_frame_context;
	}

	if (do_next_frame_context)
		next_frame_context();
}

bool Device::next_frame_context_is_non_blocking()
{
	DRAIN_FRAME_LOCK();

	uint32_t next_context = frame_context_index + 1;
	if (next_context >= per_frame.size())
		next_context = 0;

	return per_frame[next_context]->wait(0);
}

void Device::next_frame_context()
{
	DRAIN_FRAME_LOCK();

	if (frame_context_begin_ts)
	{
		auto frame_context_end_ts = write_calibrated_timestamp_nolock();
		register_time_interval_nolock("CPU", std::move(frame_context_begin_ts), std::move(frame_context_end_ts), "command submissions");
		frame_context_begin_ts = {};
	}

	// Flush the frame here as we might have pending staging command buffers from init stage.
	end_frame_nolock();

	framebuffer_allocator.begin_frame();
	transient_allocator.begin_frame();

	if (!ext.supports_descriptor_buffer)
	{
		for (auto &allocator: descriptor_set_allocators.get_read_only())
			allocator.begin_frame();
		for (auto &allocator: descriptor_set_allocators.get_read_write())
			allocator.begin_frame();
	}

	VK_ASSERT(!per_frame.empty());
	frame_context_index++;
	if (frame_context_index >= per_frame.size())
		frame_context_index = 0;

	promote_read_write_caches_to_read_only();

	frame().begin();
	recalibrate_timestamps();
	frame_context_begin_ts = write_calibrated_timestamp_nolock();
}

QueryPoolHandle Device::write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlags2 stage)
{
	LOCK();
	return write_timestamp_nolock(cmd, stage);
}

QueryPoolHandle Device::write_timestamp_nolock(VkCommandBuffer cmd, VkPipelineStageFlags2 stage)
{
	return frame().query_pool.write_timestamp(cmd, stage);
}

QueryPoolHandle Device::write_calibrated_timestamp()
{
	LOCK();
	return write_calibrated_timestamp_nolock();
}

QueryPoolHandle Device::write_calibrated_timestamp_nolock()
{
	if (!system_handles.timeline_trace_file)
		return {};

	auto handle = QueryPoolHandle(handle_pool.query.allocate(this, false));
	handle->signal_timestamp_ticks(get_current_time_nsecs());
	return handle;
}

void Device::recalibrate_timestamps_fallback()
{
	wait_idle_nolock();
	auto cmd = request_command_buffer_nolock(0, CommandBuffer::Type::Generic, false);
	auto ts = write_timestamp_nolock(cmd->get_command_buffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	if (!ts)
	{
		submit_discard_nolock(cmd);
		return;
	}
	auto start_ts = Util::get_current_time_nsecs();
	submit_nolock(cmd, nullptr, 0, nullptr);
	wait_idle_nolock();
	auto end_ts = Util::get_current_time_nsecs();
	auto host_ts = (start_ts + end_ts) / 2;

	LOGI("Calibrated timestamps with a fallback method. Uncertainty: %.3f us.\n", 1e-3 * (end_ts - start_ts));

	calibrated_timestamp_host = host_ts;
	VK_ASSERT(ts->is_signalled());
	calibrated_timestamp_device = ts->get_timestamp_ticks();
	calibrated_timestamp_device_accum = calibrated_timestamp_device;
}

void Device::init_calibrated_timestamps()
{
	if (!get_device_features().supports_calibrated_timestamps)
	{
		recalibrate_timestamps_fallback();
		return;
	}

	uint32_t count;
	vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(gpu, &count, nullptr);
	std::vector<VkTimeDomainEXT> domains(count);
	if (vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(gpu, &count, domains.data()) != VK_SUCCESS)
		return;

	bool supports_device_domain = false;
	for (auto &domain : domains)
	{
		if (domain == VK_TIME_DOMAIN_DEVICE_KHR)
		{
			supports_device_domain = true;
			break;
		}
	}

	if (!supports_device_domain)
		return;

	for (auto &domain : domains)
	{
#ifdef _WIN32
		const auto supported_domain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
#elif defined(ANDROID)
		const auto supported_domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
#else
		const auto supported_domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR;
#endif
		if (domain == supported_domain)
		{
			calibrated_time_domain = domain;
			break;
		}
	}

	if (calibrated_time_domain == VK_TIME_DOMAIN_DEVICE_KHR)
	{
		LOGE("Could not find a suitable time domain for calibrated timestamps.\n");
		return;
	}

	if (!resample_calibrated_timestamps())
	{
		LOGE("Failed to get calibrated timestamps.\n");
		calibrated_time_domain = VK_TIME_DOMAIN_DEVICE_KHR;
		return;
	}
}

bool Device::resample_calibrated_timestamps()
{
	VkCalibratedTimestampInfoKHR infos[2] = {};
	infos[0].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
	infos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
	infos[0].timeDomain = calibrated_time_domain;
	infos[1].timeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
	uint64_t timestamps[2] = {};
	uint64_t max_deviation;

	if (table->vkGetCalibratedTimestampsKHR(device, 2, infos, timestamps, &max_deviation) != VK_SUCCESS)
	{
		LOGE("Failed to get calibrated timestamps.\n");
		calibrated_time_domain = VK_TIME_DOMAIN_DEVICE_KHR;
		return false;
	}

	calibrated_timestamp_host = timestamps[0];
	calibrated_timestamp_device = timestamps[1];
	calibrated_timestamp_device_accum = calibrated_timestamp_device;

#ifdef _WIN32
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	calibrated_timestamp_host = int64_t(1e9 * calibrated_timestamp_host / double(freq.QuadPart));
#endif
	return true;
}

void Device::recalibrate_timestamps()
{
	// Don't bother recalibrating timestamps if we're not tracing.
	if (!system_handles.timeline_trace_file)
		return;

	// Recalibrate every once in a while ...
	timestamp_calibration_counter++;
	if (timestamp_calibration_counter < 1000)
		return;
	timestamp_calibration_counter = 0;

	if (calibrated_time_domain == VK_TIME_DOMAIN_DEVICE_EXT)
		recalibrate_timestamps_fallback();
	else
		resample_calibrated_timestamps();
}

void Device::register_time_interval(std::string tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts,
                                    const std::string &tag)
{
	LOCK();
	register_time_interval_nolock(std::move(tid), std::move(start_ts), std::move(end_ts), tag);
}

void Device::register_time_interval_nolock(std::string tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts,
                                           const std::string &tag)
{
	if (start_ts && end_ts)
	{
		TimestampInterval *timestamp_tag = managers.timestamps.get_timestamp_tag(tag.c_str());
#ifdef VULKAN_DEBUG
		if (start_ts->is_signalled() && end_ts->is_signalled())
			VK_ASSERT(end_ts->get_timestamp_ticks() >= start_ts->get_timestamp_ticks());
#endif
		frame().timestamp_intervals.push_back({ std::move(tid), std::move(start_ts), std::move(end_ts), timestamp_tag });
	}
}

void Device::add_frame_counter_nolock()
{
	lock.counter++;
}

void Device::decrement_frame_counter_nolock()
{
	VK_ASSERT(lock.counter > 0);
	lock.counter--;
	lock.cond.notify_all();
}

void Device::PerFrame::trim_command_pools()
{
	for (auto &cmd_pool : cmd_pools)
		for (auto &pool : cmd_pool)
			pool.trim();
}

bool Device::PerFrame::wait(uint64_t timeout)
{
	VkDevice vkdevice = device.get_device();
	bool has_timeline = true;

	for (auto &sem : timeline_semaphores)
	{
		if (sem == VK_NULL_HANDLE)
		{
			has_timeline = false;
			break;
		}
	}

	if (device.get_device_features().vk12_features.timelineSemaphore && has_timeline)
	{
		VkSemaphoreWaitInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		VkSemaphore sems[QUEUE_INDEX_COUNT];
		uint64_t values[QUEUE_INDEX_COUNT];
		for (int i = 0; i < QUEUE_INDEX_COUNT; i++)
		{
			if (timeline_fences[i])
			{
				sems[info.semaphoreCount] = timeline_semaphores[i];
				values[info.semaphoreCount] = timeline_fences[i];
				info.semaphoreCount++;
			}
		}

		if (info.semaphoreCount)
		{
			info.pSemaphores = sems;
			info.pValues = values;
			if (table.vkWaitSemaphores(vkdevice, &info, timeout) != VK_SUCCESS)
				return false;
		}
	}

	// If we're using timeline semaphores, these paths should never be hit (or only for swapchain maintenance1).
	if (!wait_and_recycle_fences.empty())
	{
		if (table.vkWaitForFences(vkdevice, wait_and_recycle_fences.size(), wait_and_recycle_fences.data(), VK_TRUE, timeout) != VK_SUCCESS)
			return false;
		table.vkResetFences(vkdevice, wait_and_recycle_fences.size(), wait_and_recycle_fences.data());
		for (auto &fence : wait_and_recycle_fences)
			managers.fence.recycle_fence(fence);
		wait_and_recycle_fences.clear();
	}

	return true;
}

void Device::PerFrame::begin()
{
	VkDevice vkdevice = device.get_device();

	Vulkan::QueryPoolHandle wait_fence_ts;
	if (!in_destructor)
		wait_fence_ts = device.write_calibrated_timestamp_nolock();

	wait(UINT64_MAX);

	for (auto &cmd_pool : cmd_pools)
		for (auto &pool : cmd_pool)
			pool.begin();

	query_pool.begin();

	for (auto &channel : debug_channels)
		device.parse_debug_channel(channel);

	// Free the debug channel buffers here, and they will immediately be recycled by the destroyed_buffers right below.
	debug_channels.clear();

	for (auto &block : vbo_blocks)
		managers.vbo.recycle_block(block);
	for (auto &block : ibo_blocks)
		managers.ibo.recycle_block(block);
	for (auto &block : ubo_blocks)
		managers.ubo.recycle_block(block);
	for (auto &block : staging_blocks)
		managers.staging.recycle_block(block);
	vbo_blocks.clear();
	ibo_blocks.clear();
	ubo_blocks.clear();
	staging_blocks.clear();

	for (auto &framebuffer : destroyed_framebuffers)
		table.vkDestroyFramebuffer(vkdevice, framebuffer, nullptr);
	for (auto &sampler : destroyed_samplers)
		table.vkDestroySampler(vkdevice, sampler, nullptr);
	for (auto &view : destroyed_image_views)
		table.vkDestroyImageView(vkdevice, view, nullptr);
	for (auto &view : destroyed_buffer_views)
		table.vkDestroyBufferView(vkdevice, view, nullptr);
	for (auto &image : destroyed_images)
		table.vkDestroyImage(vkdevice, image, nullptr);
	for (auto &buffer : destroyed_buffers)
		table.vkDestroyBuffer(vkdevice, buffer, nullptr);
	for (auto &semaphore : destroyed_semaphores)
		table.vkDestroySemaphore(vkdevice, semaphore, nullptr);
	for (auto &pool : destroyed_descriptor_pools)
		table.vkDestroyDescriptorPool(vkdevice, pool, nullptr);
	for (auto &exec_set : destroyed_execution_sets)
		table.vkDestroyIndirectExecutionSetEXT(vkdevice, exec_set, nullptr);
	for (auto &semaphore : recycled_semaphores)
		managers.semaphore.recycle(semaphore);
	for (auto &event : recycled_events)
		managers.event.recycle(event);
	managers.descriptor_buffer.free(descriptor_buffer_allocs.data(), descriptor_buffer_allocs.size());
	managers.descriptor_buffer.free_cached_descriptors(
			cached_descriptor_payloads.data(), cached_descriptor_payloads.size());
	VK_ASSERT(consumed_semaphores.empty());

	if (!allocations.empty())
	{
		std::lock_guard<std::mutex> holder{device.lock.memory_lock};
		for (auto &alloc : allocations)
			alloc.free_immediate(managers.memory);
	}

	destroyed_framebuffers.clear();
	destroyed_samplers.clear();
	destroyed_image_views.clear();
	destroyed_buffer_views.clear();
	destroyed_images.clear();
	destroyed_buffers.clear();
	destroyed_execution_sets.clear();
	destroyed_semaphores.clear();
	destroyed_descriptor_pools.clear();
	recycled_semaphores.clear();
	recycled_events.clear();
	allocations.clear();
	descriptor_buffer_allocs.clear();
	cached_descriptor_payloads.clear();

	if (!in_destructor)
		device.register_time_interval_nolock("CPU", std::move(wait_fence_ts), device.write_calibrated_timestamp_nolock(), "fence + recycle");

	int64_t min_timestamp_us = std::numeric_limits<int64_t>::max();
	int64_t max_timestamp_us = 0;

	for (auto &ts : timestamp_intervals)
	{
		if (ts.end_ts->is_signalled() && ts.start_ts->is_signalled())
		{
			VK_ASSERT(ts.start_ts->is_device_timebase() == ts.end_ts->is_device_timebase());

			int64_t start_ts = ts.start_ts->get_timestamp_ticks();
			int64_t end_ts = ts.end_ts->get_timestamp_ticks();
			if (ts.start_ts->is_device_timebase())
				ts.timestamp_tag->accumulate_time(device.convert_device_timestamp_delta(start_ts, end_ts));
			else
				ts.timestamp_tag->accumulate_time(1e-9 * double(end_ts - start_ts));

			if (device.system_handles.timeline_trace_file)
			{
				start_ts = device.convert_timestamp_to_absolute_nsec(*ts.start_ts);
				end_ts = device.convert_timestamp_to_absolute_nsec(*ts.end_ts);
				min_timestamp_us = (std::min)(min_timestamp_us, start_ts);
				max_timestamp_us = (std::max)(max_timestamp_us, end_ts);

				auto *e = device.system_handles.timeline_trace_file->allocate_event();
				e->set_desc(ts.timestamp_tag->get_tag().c_str());
				e->set_tid(ts.tid.c_str());
				e->pid = frame_index + 1;
				e->start_ns = start_ts;
				e->end_ns = end_ts;
				device.system_handles.timeline_trace_file->submit_event(e);
			}
		}
	}

	if (device.system_handles.timeline_trace_file && min_timestamp_us <= max_timestamp_us)
	{
		auto *e = device.system_handles.timeline_trace_file->allocate_event();
		e->set_desc("CPU + GPU full frame");
		e->set_tid("Frame context");
		e->pid = frame_index + 1;
		e->start_ns = min_timestamp_us;
		e->end_ns = max_timestamp_us;
		device.system_handles.timeline_trace_file->submit_event(e);
	}

	managers.timestamps.mark_end_of_frame_context();
	timestamp_intervals.clear();
}

Device::PerFrame::~PerFrame()
{
	in_destructor = true;
	begin();
}

uint32_t Device::find_memory_type(uint32_t required, uint32_t mask) const
{
	uint32_t valid_device_local_mask = 0;
	uint32_t valid_mask = 0;

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if (((1u << i) & mask) != 0)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & required) == required)
			{
				valid_mask |= 1u << i;
				if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
					valid_device_local_mask |= 1u << i;
			}
		}
	}

	// If we don't request device local, try to avoid it.
	// Avoids a quirk of NVK where we end up allocating device memory instead since DEVICE | COHERENT
	// appears before COHERENT | CACHED.
	if ((required & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0 && valid_mask != valid_device_local_mask)
		valid_mask &= ~valid_device_local_mask;

	if (valid_mask != 0)
		return Util::trailing_zeroes(valid_mask);
	else
		return UINT32_MAX;
}

uint32_t Device::find_memory_type(BufferDomain domain, uint32_t mask) const
{
	uint32_t prio[3] = {};

	// Optimize for tracing apps by not allocating host memory that is uncached.
	if (workarounds.force_host_cached)
	{
		switch (domain)
		{
		case BufferDomain::LinkedDeviceHostPreferDevice:
			domain = BufferDomain::Device;
			break;

		case BufferDomain::LinkedDeviceHost:
		case BufferDomain::Host:
		case BufferDomain::CachedCoherentHostPreferCoherent:
			domain = BufferDomain::CachedCoherentHostPreferCached;
			break;

		default:
			break;
		}
	}

	switch (domain)
	{
	case BufferDomain::Device:
		prio[0] = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;

	case BufferDomain::LinkedDeviceHost:
		prio[0] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		prio[1] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		prio[2] = prio[1];
		break;

	case BufferDomain::LinkedDeviceHostPreferDevice:
		prio[0] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		prio[1] = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		prio[2] = prio[1];
		break;

	case BufferDomain::Host:
		prio[0] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		prio[1] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		prio[2] = prio[1];
		break;

	case BufferDomain::CachedHost:
		prio[0] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		prio[1] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		prio[2] = prio[1];
		break;

	case BufferDomain::CachedCoherentHostPreferCached:
		prio[0] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		prio[1] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		prio[2] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;

	case BufferDomain::CachedCoherentHostPreferCoherent:
		prio[0] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		prio[1] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		prio[2] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;

	case BufferDomain::UMACachedCoherentPreferDevice:
		prio[0] = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		          VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
		          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		prio[1] = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		          VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

		// On iGPU, we expect to find a UMA type, but RADV tends to report split heaps on iGPU for app compat reasons.
		// If the device type is integrated we just assume that host visible memory isn't meaningfully slower than
		// "device local" memory.
		if (gpu_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
			prio[2] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		else
			prio[2] = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

		break;
	}

	for (auto &p : prio)
	{
		uint32_t index = find_memory_type(p, mask);
		if (index != UINT32_MAX)
			return index;
	}

	return UINT32_MAX;
}

uint32_t Device::find_memory_type(ImageDomain domain, uint32_t mask) const
{
	uint32_t desired = 0, fallback = 0;
	switch (domain)
	{
	case ImageDomain::Physical:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case ImageDomain::Transient:
		desired = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
		fallback = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		break;

	case ImageDomain::LinearHostCached:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;

	case ImageDomain::LinearHost:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		fallback = 0;
		break;

	case ImageDomain::LinearDevice:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case ImageDomain::HostCopy:
		desired = 0;
		fallback = 0;
		break;
	}

	uint32_t index = find_memory_type(desired, mask);
	if (index != UINT32_MAX)
		return index;

	index = find_memory_type(fallback, mask);
	if (index != UINT32_MAX)
		return index;

	return UINT32_MAX;
}

static inline VkImageViewType get_image_view_type(const ImageCreateInfo &create_info, const ImageViewCreateInfo *view)
{
	unsigned layers = view ? view->layers : create_info.layers;
	unsigned base_layer = view ? view->base_layer : 0;

	if (layers == VK_REMAINING_ARRAY_LAYERS)
		layers = create_info.layers - base_layer;

	bool force_array =
	    view ? (view->misc & IMAGE_VIEW_MISC_FORCE_ARRAY_BIT) : (create_info.misc & IMAGE_MISC_FORCE_ARRAY_BIT);

	switch (create_info.type)
	{
	case VK_IMAGE_TYPE_1D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height == 1);
		VK_ASSERT(create_info.depth == 1);
		VK_ASSERT(create_info.samples == VK_SAMPLE_COUNT_1_BIT);

		if (layers > 1 || force_array)
			return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
		else
			return VK_IMAGE_VIEW_TYPE_1D;

	case VK_IMAGE_TYPE_2D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height >= 1);
		VK_ASSERT(create_info.depth == 1);

		if ((create_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && (layers % 6) == 0)
		{
			VK_ASSERT(create_info.width == create_info.height);

			if (layers > 6 || force_array)
				return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_CUBE;
		}
		else
		{
			if (layers > 1 || force_array)
				return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_2D;
		}

	case VK_IMAGE_TYPE_3D:
		VK_ASSERT(create_info.width >= 1);
		VK_ASSERT(create_info.height >= 1);
		VK_ASSERT(create_info.depth >= 1);
		return VK_IMAGE_VIEW_TYPE_3D;

	default:
		VK_ASSERT(0 && "bogus");
		return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	}
}

BufferViewHandle Device::create_buffer_view(const BufferViewCreateInfo &view_info)
{
	if (ext.supports_descriptor_buffer)
	{
		VkDescriptorAddressInfoEXT addr = { VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT };
		VkDescriptorGetInfoEXT info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
		CachedDescriptorPayload ro = {};
		CachedDescriptorPayload rw = {};

		addr.address = view_info.buffer->get_device_address() + view_info.offset;
		if (view_info.range == VK_WHOLE_SIZE)
			addr.range = view_info.buffer->get_create_info().size - view_info.offset;
		else
			addr.range = view_info.range;
		addr.format = view_info.format;

		VkFormatProperties3 props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
		get_format_properties(view_info.format, &props3);

		if ((view_info.buffer->get_create_info().usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) != 0 &&
		    (props3.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT) != 0)
		{
			ro = managers.descriptor_buffer.alloc_uniform_texel();
			info.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			info.data.pUniformTexelBuffer = &addr;
			table->vkGetDescriptorEXT(
					device, &info, managers.descriptor_buffer.get_descriptor_size_for_type(info.type), ro.ptr);
		}

		if ((view_info.buffer->get_create_info().usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) != 0 &&
		    (props3.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT) != 0)
		{
			rw = managers.descriptor_buffer.alloc_storage_texel();
			info.type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
			info.data.pStorageTexelBuffer = &addr;
			table->vkGetDescriptorEXT(
					device, &info, managers.descriptor_buffer.get_descriptor_size_for_type(info.type), rw.ptr);
		}

		return BufferViewHandle(handle_pool.buffer_views.allocate(this, ro, rw, view_info));
	}
	else
	{
		VkBufferViewCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		info.buffer = view_info.buffer->get_buffer();
		info.format = view_info.format;
		info.offset = view_info.offset;
		info.range = view_info.range;

		VkBufferView view;
		auto res = table->vkCreateBufferView(device, &info, nullptr, &view);
		if (res != VK_SUCCESS)
			return BufferViewHandle(nullptr);

		return BufferViewHandle(handle_pool.buffer_views.allocate(this, view, view_info));
	}
}

class ImageResourceHolder
{
public:
	explicit ImageResourceHolder(Device *device_)
		: device(device_)
		, table(device_->get_device_table())
	{
	}

	~ImageResourceHolder()
	{
		if (owned)
			cleanup();
	}

	Device *device;
	const VolkDeviceTable &table;

	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView image_view = VK_NULL_HANDLE;
	VkImageView depth_view = VK_NULL_HANDLE;
	VkImageView stencil_view = VK_NULL_HANDLE;
	VkImageView unorm_view = VK_NULL_HANDLE;
	VkImageView srgb_view = VK_NULL_HANDLE;
	VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	std::vector<VkImageView> rt_views;
	std::vector<VkImageView> mip_views;
	DeviceAllocation allocation;
	DeviceAllocator *allocator = nullptr;
	bool owned = true;

	VkImageViewType get_default_view_type() const
	{
		return default_view_type;
	}

	bool setup_conversion_info(VkImageViewCreateInfo &create_info,
	                           VkSamplerYcbcrConversionInfo &conversion,
	                           const ImmutableYcbcrConversion *ycbcr_conversion) const
	{
		if (ycbcr_conversion)
		{
			if (!device->get_device_features().vk11_features.samplerYcbcrConversion)
				return false;
			conversion = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
			conversion.conversion = ycbcr_conversion->get_conversion();
			conversion.pNext = create_info.pNext;
			create_info.pNext = &conversion;
		}

		return true;
	}

	bool setup_view_usage_info(VkImageViewCreateInfo &create_info, VkImageUsageFlags usage,
	                           VkImageViewUsageCreateInfo &usage_info) const
	{
		usage_info.usage = usage;
		usage_info.usage &= VK_IMAGE_USAGE_SAMPLED_BIT |
		                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
		                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		                    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
		                    image_usage_video_flags;

		if (format_is_srgb(create_info.format))
			usage_info.usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;

		usage_info.pNext = create_info.pNext;
		create_info.pNext = &usage_info;

		return true;
	}

	bool setup_astc_decode_mode_info(VkImageViewCreateInfo &create_info, VkImageViewASTCDecodeModeEXT &astc_info) const
	{
		if (!device->get_device_features().supports_astc_decode_mode)
			return true;

		auto type = format_compression_type(create_info.format);
		if (type != FormatCompressionType::ASTC)
			return true;

		if (format_is_srgb(create_info.format))
			return true;

		if (format_is_compressed_hdr(create_info.format))
		{
			if (device->get_device_features().astc_decode_features.decodeModeSharedExponent)
				astc_info.decodeMode = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
			else
				astc_info.decodeMode = VK_FORMAT_R16G16B16A16_SFLOAT;
		}
		else
		{
			astc_info.decodeMode = VK_FORMAT_R8G8B8A8_UNORM;
		}

		astc_info.pNext = create_info.pNext;
		create_info.pNext = &astc_info;
		return true;
	}

	bool create_default_views(const ImageCreateInfo &create_info, const VkImageViewCreateInfo *view_info,
	                          const ImmutableYcbcrConversion *ycbcr_conversion,
	                          bool create_unorm_srgb_views = false, bool create_mip_level_views = false,
	                          const VkFormat *view_formats = nullptr)
	{
		VkDevice vkdevice = device->get_device();

		if ((create_info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
		                          image_usage_video_flags)) == 0)
		{
			LOGE("Cannot create image view unless certain usage flags are present.\n");
			return false;
		}

		VkImageViewCreateInfo default_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		VkSamplerYcbcrConversionInfo conversion_info = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
		VkImageViewUsageCreateInfo view_usage_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO };
		VkImageViewASTCDecodeModeEXT astc_decode_mode_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_ASTC_DECODE_MODE_EXT };

		if (!view_info)
		{
			default_view_info.image = image;
			default_view_info.format = create_info.format;
			default_view_info.components = create_info.swizzle;
			default_view_info.subresourceRange.aspectMask = format_to_aspect_mask(default_view_info.format);
			default_view_info.viewType = get_image_view_type(create_info, nullptr);
			default_view_info.subresourceRange.baseMipLevel = 0;
			default_view_info.subresourceRange.baseArrayLayer = 0;
			default_view_info.subresourceRange.levelCount = create_info.levels;
			default_view_info.subresourceRange.layerCount = create_info.layers;

			default_view_type = default_view_info.viewType;
		}
		else
			default_view_info = *view_info;

		view_info = &default_view_info;
		if (!setup_conversion_info(default_view_info, conversion_info, ycbcr_conversion))
			return false;

		if (!setup_view_usage_info(default_view_info, create_info.usage, view_usage_info))
			return false;

		if (!setup_astc_decode_mode_info(default_view_info, astc_decode_mode_info))
			return false;

		if (!create_alt_views(create_info, *view_info))
			return false;

		if (!create_render_target_views(create_info, *view_info))
			return false;

		if (!create_default_view(*view_info))
			return false;

		if (create_unorm_srgb_views)
		{
			auto info = *view_info;

			if (create_info.usage & VK_IMAGE_USAGE_STORAGE_BIT)
				view_usage_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

			info.format = view_formats[0];
			if (table.vkCreateImageView(vkdevice, &info, nullptr, &unorm_view) != VK_SUCCESS)
				return false;

			view_usage_info.usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;

			info.format = view_formats[1];
			if (table.vkCreateImageView(vkdevice, &info, nullptr, &srgb_view) != VK_SUCCESS)
				return false;
		}

		if (create_mip_level_views && !create_mip_views(*view_info))
			return false;

		return true;
	}

private:
	bool create_render_target_views(const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info)
	{
		if (info.viewType == VK_IMAGE_VIEW_TYPE_3D)
			return true;

		rt_views.reserve(info.subresourceRange.layerCount);

		// If we have a render target, and non-trivial case (layers = 1, levels = 1),
		// create an array of render targets which correspond to each layer (mip 0).
		if ((image_create_info.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0 &&
		    ((info.subresourceRange.levelCount > 1) || (info.subresourceRange.layerCount > 1)))
		{
			auto view_info = info;
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.subresourceRange.baseMipLevel = info.subresourceRange.baseMipLevel;
			for (uint32_t layer = 0; layer < info.subresourceRange.layerCount; layer++)
			{
				view_info.subresourceRange.levelCount = 1;
				view_info.subresourceRange.layerCount = 1;
				view_info.subresourceRange.baseArrayLayer = layer + info.subresourceRange.baseArrayLayer;

				VkImageView rt_view;
				if (table.vkCreateImageView(device->get_device(), &view_info, nullptr, &rt_view) != VK_SUCCESS)
					return false;

				rt_views.push_back(rt_view);
			}
		}

		return true;
	}

	bool create_mip_views(const VkImageViewCreateInfo &info)
	{
		VK_ASSERT(info.subresourceRange.levelCount != VK_REMAINING_MIP_LEVELS);
		if (info.subresourceRange.levelCount <= 1)
			return true;
		mip_views.reserve(info.subresourceRange.levelCount);

		auto view_info = info;

		for (unsigned level = 0; level < info.subresourceRange.levelCount; level++)
		{
			view_info.subresourceRange.baseMipLevel = level;
			view_info.subresourceRange.levelCount = 1;
			VkImageView mip_view;

			if (table.vkCreateImageView(device->get_device(), &view_info, nullptr, &mip_view) != VK_SUCCESS)
				return false;

			mip_views.push_back(mip_view);
		}

		return true;
	}

	bool create_alt_views(const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info)
	{
		if (info.viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
		    info.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY ||
		    info.viewType == VK_IMAGE_VIEW_TYPE_3D)
		{
			return true;
		}

		VkDevice vkdevice = device->get_device();

		if (info.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
		{
			if ((image_create_info.usage & ~VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
			{
				auto view_info = info;

				// We need this to be able to sample the texture, or otherwise use it as a non-pure DS attachment.
				view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				if (table.vkCreateImageView(vkdevice, &view_info, nullptr, &depth_view) != VK_SUCCESS)
					return false;

				view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
				if (table.vkCreateImageView(vkdevice, &view_info, nullptr, &stencil_view) != VK_SUCCESS)
					return false;
			}
		}

		return true;
	}

	bool create_default_view(const VkImageViewCreateInfo &info)
	{
		VkDevice vkdevice = device->get_device();

		// Create the normal image view. This one contains every subresource.
		if (table.vkCreateImageView(vkdevice, &info, nullptr, &image_view) != VK_SUCCESS)
			return false;

		return true;
	}

	void cleanup()
	{
		VkDevice vkdevice = device->get_device();

		if (image_view)
			table.vkDestroyImageView(vkdevice, image_view, nullptr);
		if (depth_view)
			table.vkDestroyImageView(vkdevice, depth_view, nullptr);
		if (stencil_view)
			table.vkDestroyImageView(vkdevice, stencil_view, nullptr);
		if (unorm_view)
			table.vkDestroyImageView(vkdevice, unorm_view, nullptr);
		if (srgb_view)
			table.vkDestroyImageView(vkdevice, srgb_view, nullptr);
		for (auto &view : rt_views)
			table.vkDestroyImageView(vkdevice, view, nullptr);
		for (auto &view : mip_views)
			table.vkDestroyImageView(vkdevice, view, nullptr);

		if (image)
			table.vkDestroyImage(vkdevice, image, nullptr);
		if (memory)
			table.vkFreeMemory(vkdevice, memory, nullptr);
		if (allocator)
			allocation.free_immediate(*allocator);
	}
};

ImageViewHandle Device::create_image_view(const ImageViewCreateInfo &create_info)
{
	ImageResourceHolder holder(this);
	auto &image_create_info = create_info.image->get_create_info();

	VkFormat format = create_info.format != VK_FORMAT_UNDEFINED ? create_info.format : image_create_info.format;

	VkImageUsageFlags usage = create_info.image->get_create_info().usage;
	VkFormatProperties3 props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
	get_format_properties(format, &props3);
	if ((props3.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
		usage &= ~VK_IMAGE_USAGE_SAMPLED_BIT;
	if ((props3.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
		usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = create_info.image->get_image();
	view_info.format = format;
	view_info.components = create_info.swizzle;
	view_info.subresourceRange.aspectMask =
			create_info.aspect ? create_info.aspect : format_to_aspect_mask(format);
	view_info.subresourceRange.baseMipLevel = create_info.base_level;
	view_info.subresourceRange.baseArrayLayer = create_info.base_layer;
	view_info.subresourceRange.levelCount = create_info.levels;
	view_info.subresourceRange.layerCount = create_info.layers;

	if (create_info.view_type == VK_IMAGE_VIEW_TYPE_MAX_ENUM)
		view_info.viewType = get_image_view_type(image_create_info, &create_info);
	else
		view_info.viewType = create_info.view_type;

	unsigned num_levels;
	if (view_info.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS)
		num_levels = create_info.image->get_create_info().levels - view_info.subresourceRange.baseMipLevel;
	else
		num_levels = view_info.subresourceRange.levelCount;

	unsigned num_layers;
	if (view_info.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS)
		num_layers = create_info.image->get_create_info().layers - view_info.subresourceRange.baseArrayLayer;
	else
		num_layers = view_info.subresourceRange.layerCount;

	view_info.subresourceRange.levelCount = num_levels;
	view_info.subresourceRange.layerCount = num_layers;

	if (!holder.create_default_views(image_create_info, &view_info,
	                                 create_info.ycbcr_conversion))
	{
		return ImageViewHandle(nullptr);
	}

	ImageViewCreateInfo tmp = create_info;
	tmp.format = format;
	ImageViewHandle ret(handle_pool.image_views.allocate(this, holder.image_view, tmp, usage));
	if (ret)
	{
		holder.owned = false;
		ret->set_alt_views(holder.depth_view, holder.stencil_view);
		ret->set_render_target_views(std::move(holder.rt_views));
		ret->set_mip_views(std::move(holder.mip_views));
		ret->rebuild_cached_descriptor_payloads(create_info.image->get_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		return ret;
	}
	else
		return ImageViewHandle(nullptr);
}

InitialImageBuffer Device::create_image_staging_buffer(const TextureFormatLayout &layout)
{
	InitialImageBuffer result;
	result.host = { layout.data(), layout.get_required_size() };
	layout.build_buffer_image_copies(result.blits);
	return result;
}

InitialImageBuffer Device::create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial)
{
	// This method is very annoying to deal with and requires shuffling a lot of data around.
	// Plumbing this through to host image copy is a hot mess and is avoided.

	InitialImageBuffer result;

	bool generate_mips = (info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;
	TextureFormatLayout layout;

	unsigned copy_levels;
	if (generate_mips)
		copy_levels = 1;
	else if (info.levels == 0)
		copy_levels = TextureFormatLayout::num_miplevels(info.width, info.height, info.depth);
	else
		copy_levels = info.levels;

	switch (info.type)
	{
	case VK_IMAGE_TYPE_1D:
		layout.set_1d(info.format, info.width, info.layers, copy_levels);
		break;
	case VK_IMAGE_TYPE_2D:
		layout.set_2d(info.format, info.width, info.height, info.layers, copy_levels);
		break;
	case VK_IMAGE_TYPE_3D:
		layout.set_3d(info.format, info.width, info.height, info.depth, copy_levels);
		break;
	default:
		return {};
	}

	if (copy_levels == 1 && info.layers == 1)
	{
		result.host = { initial[0].data, layout.get_required_size() };
		layout.build_buffer_image_copies(result.blits);
		auto &blit = result.blits.front();
		const auto &mip_info = layout.get_mip_info(0);

		// Adjust the blit in case it's not tightly packed.
		uint32_t src_row_length =
				initial[0].row_length ? initial[0].row_length : mip_info.row_length;
		uint32_t src_array_height =
				initial[0].image_height ? initial[0].image_height : mip_info.image_height;

		result.host.size = format_get_layer_size(
				info.format, blit.imageSubresource.aspectMask, src_row_length, src_array_height, info.depth);

		blit.bufferOffset = 0;
		blit.bufferRowLength = src_row_length;
		blit.bufferImageHeight = src_array_height;
		return result;
	}

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Host;
	buffer_info.size = layout.get_required_size();
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	{
		GRANITE_SCOPED_TIMELINE_EVENT_FILE(system_handles.timeline_trace_file, "allocate-image-staging-buffer");
		result.buffer = create_buffer(buffer_info, nullptr);
	}
	set_name(*result.buffer, "image-upload-staging-buffer");

	// And now, do the actual copy.
	auto *mapped = static_cast<uint8_t *>(map_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
	unsigned index = 0;

	layout.set_buffer(mapped, layout.get_required_size());

	GRANITE_SCOPED_TIMELINE_EVENT_FILE(system_handles.timeline_trace_file, "copy-image-staging-buffer");
	for (unsigned level = 0; level < copy_levels; level++)
	{
		const auto &mip_info = layout.get_mip_info(level);
		uint32_t dst_height_stride = layout.get_layer_size(level);
		size_t row_size = layout.get_row_size(level);

		for (unsigned layer = 0; layer < info.layers; layer++, index++)
		{
			uint32_t src_row_length =
					initial[index].row_length ? initial[index].row_length : mip_info.row_length;
			uint32_t src_array_height =
					initial[index].image_height ? initial[index].image_height : mip_info.image_height;

			uint32_t src_row_stride = layout.row_byte_stride(src_row_length);
			uint32_t src_height_stride = layout.layer_byte_stride(src_array_height, src_row_stride);

			auto *dst = static_cast<uint8_t *>(layout.data(layer, level));
			const auto *src = static_cast<const uint8_t *>(initial[index].data);

			for (uint32_t z = 0; z < mip_info.depth; z++)
				for (uint32_t y = 0; y < mip_info.block_image_height; y++)
					memcpy(dst + z * dst_height_stride + y * row_size, src + z * src_height_stride + y * src_row_stride, row_size);
		}
	}

	unmap_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);
	layout.build_buffer_image_copies(result.blits);
	return result;
}

DeviceAllocationOwnerHandle Device::take_device_allocation_ownership(Image &image)
{
	if ((image.get_create_info().misc & IMAGE_MISC_FORCE_NO_DEDICATED_BIT) == 0)
	{
		LOGE("Must use FORCE_NO_DEDICATED_BIT to take ownership of memory.\n");
		return DeviceAllocationOwnerHandle{};
	}

	if (!image.get_allocation().alloc || !image.get_allocation().base)
		return DeviceAllocationOwnerHandle{};

	return DeviceAllocationOwnerHandle(handle_pool.allocations.allocate(this, image.take_allocation_ownership()));
}

DeviceAllocationOwnerHandle Device::allocate_memory(const MemoryAllocateInfo &info)
{
	uint32_t index = find_memory_type(info.required_properties, info.requirements.memoryTypeBits);
	if (index == UINT32_MAX)
		return {};

	DeviceAllocation alloc = {};
	{
		LOCK_MEMORY();
		if (!managers.memory.allocate_generic_memory(info.requirements.size, info.requirements.alignment, info.mode,
		                                             index, &alloc))
		{
			return {};
		}
	}
	return DeviceAllocationOwnerHandle(handle_pool.allocations.allocate(this, alloc));
}

void Device::get_memory_budget(HeapBudget *budget)
{
	LOCK_MEMORY();
	managers.memory.get_memory_budget(budget);
}

ImageHandle Device::create_image(const ImageCreateInfo &create_info, const ImageInitialData *initial)
{
	if (initial)
	{
		auto staging_buffer = create_image_staging_buffer(create_info, initial);
		return create_image_from_staging_buffer(create_info, &staging_buffer);
	}
	else
		return create_image_from_staging_buffer(create_info, nullptr);
}

bool Device::allocate_image_memory(DeviceAllocation *allocation, const ImageCreateInfo &info,
                                   VkImage image, VkImageTiling tiling, VkImageUsageFlags usage)
{
	if ((info.flags & VK_IMAGE_CREATE_DISJOINT_BIT) != 0 && info.num_memory_aliases == 0)
	{
		LOGE("Must use memory aliases when creating a DISJOINT planar image.\n");
		return false;
	}

	bool use_external = (info.misc & IMAGE_MISC_EXTERNAL_MEMORY_BIT) != 0;
	if (use_external && info.num_memory_aliases != 0)
	{
		LOGE("Cannot use external and memory aliases at the same time.\n");
		return false;
	}

	if (use_external && tiling == VK_IMAGE_TILING_LINEAR)
	{
		LOGE("Cannot use linear tiling with external memory.\n");
		return false;
	}

	if (info.num_memory_aliases != 0)
	{
		*allocation = {};

		unsigned num_planes = format_ycbcr_num_planes(info.format);
		if (info.num_memory_aliases < num_planes)
			return false;

		if (num_planes == 1)
		{
			VkMemoryRequirements reqs;
			table->vkGetImageMemoryRequirements(device, image, &reqs);
			auto &alias = *info.memory_aliases[0];

			// Verify we can actually use this aliased allocation.
			if ((reqs.memoryTypeBits & (1u << alias.memory_type)) == 0)
				return false;
			if (reqs.size > alias.size)
				return false;
			if (((alias.offset + reqs.alignment - 1) & ~(reqs.alignment - 1)) != alias.offset)
				return false;

			if (table->vkBindImageMemory(device, image, alias.get_memory(), alias.get_offset()) != VK_SUCCESS)
				return false;
		}
		else
		{
			VkBindImageMemoryInfo bind_infos[3];
			VkBindImagePlaneMemoryInfo bind_plane_infos[3];
			VK_ASSERT(num_planes <= 3);

			for (unsigned plane = 0; plane < num_planes; plane++)
			{
				VkMemoryRequirements2 memory_req = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
				VkImageMemoryRequirementsInfo2 image_info = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
				image_info.image = image;

				VkImagePlaneMemoryRequirementsInfo plane_info = { VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO };
				plane_info.planeAspect = static_cast<VkImageAspectFlagBits>(VK_IMAGE_ASPECT_PLANE_0_BIT << plane);
				image_info.pNext = &plane_info;

				table->vkGetImageMemoryRequirements2(device, &image_info, &memory_req);
				auto &reqs = memory_req.memoryRequirements;
				auto &alias = *info.memory_aliases[plane];

				// Verify we can actually use this aliased allocation.
				if ((reqs.memoryTypeBits & (1u << alias.memory_type)) == 0)
					return false;
				if (reqs.size > alias.size)
					return false;
				if (((alias.offset + reqs.alignment - 1) & ~(reqs.alignment - 1)) != alias.offset)
					return false;

				bind_infos[plane] = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO };
				bind_infos[plane].image = image;
				bind_infos[plane].memory = alias.base;
				bind_infos[plane].memoryOffset = alias.offset;
				bind_infos[plane].pNext = &bind_plane_infos[plane];

				bind_plane_infos[plane] = { VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO };
				bind_plane_infos[plane].planeAspect = static_cast<VkImageAspectFlagBits>(VK_IMAGE_ASPECT_PLANE_0_BIT << plane);
			}

			if (table->vkBindImageMemory2(device, num_planes, bind_infos) != VK_SUCCESS)
				return false;
		}
	}
	else
	{
		VkMemoryRequirements reqs;
		table->vkGetImageMemoryRequirements(device, image, &reqs);

		// If we intend to alias with other images bump the alignment to something very high.
		// This is kind of crude, but should be high enough to allow YCbCr disjoint aliasing on any implementation.
		if (info.flags & VK_IMAGE_CREATE_ALIAS_BIT)
			if (reqs.alignment < 64 * 1024)
				reqs.alignment = 64 * 1024;

		auto domain = (usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT) != 0 ? ImageDomain::HostCopy : info.domain;
		uint32_t memory_type = find_memory_type(domain, reqs.memoryTypeBits);
		if (memory_type == UINT32_MAX)
		{
			LOGE("Failed to find memory type.\n");
			return false;
		}

		if (tiling == VK_IMAGE_TILING_LINEAR &&
		    (info.misc & IMAGE_MISC_LINEAR_IMAGE_IGNORE_DEVICE_LOCAL_BIT) == 0)
		{
			// Is it also device local?
			if ((mem_props.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0)
				return false;
		}

		ExternalHandle external = info.external;

		AllocationMode mode;
		if (use_external)
		{
			mode = AllocationMode::External;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
		         (info.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0)
		{
			mode = AllocationMode::OptimalRenderTarget;
		}
		else
		{
			mode = tiling == VK_IMAGE_TILING_OPTIMAL || info.domain == ImageDomain::LinearDevice ?
			       AllocationMode::OptimalResource : AllocationMode::LinearHostMappable;
		}

		{
			LOCK_MEMORY();
			if (!managers.memory.allocate_image_memory(reqs.size, reqs.alignment, mode, memory_type, image,
			                                           (info.misc & IMAGE_MISC_FORCE_NO_DEDICATED_BIT) != 0, allocation,
			                                           use_external ? &external : nullptr))
			{
				LOGE("Failed to allocate image memory (type %u, size: %u).\n",
				     unsigned(memory_type), unsigned(reqs.size));
				return false;
			}
		}

		if (table->vkBindImageMemory(device, image, allocation->get_memory(),
		                             allocation->get_offset()) != VK_SUCCESS)
		{
			LOGE("Failed to bind image memory.\n");
			return false;
		}
	}

	return true;
}

static void add_unique_family(uint32_t *sharing_indices, uint32_t &count, uint32_t family)
{
	if (family == VK_QUEUE_FAMILY_IGNORED)
		return;

	for (uint32_t i = 0; i < count; i++)
		if (sharing_indices[i] == family)
			return;
	sharing_indices[count++] = family;
}

ImageHandle Device::create_image_from_staging_buffer(const ImageCreateInfo &create_info,
                                                     const InitialImageBuffer *staging_buffer)
{
	ImageResourceHolder holder(this);

	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.format = create_info.format;
	info.extent.width = create_info.width;
	info.extent.height = create_info.height;
	info.extent.depth = create_info.depth;
	info.imageType = create_info.type;
	info.mipLevels = create_info.levels;
	info.arrayLayers = create_info.layers;
	info.samples = create_info.samples;
	info.pNext = create_info.pnext;

	if (create_info.domain == ImageDomain::LinearHostCached ||
	    create_info.domain == ImageDomain::LinearHost ||
	    create_info.domain == ImageDomain::LinearDevice)
	{
		info.tiling = VK_IMAGE_TILING_LINEAR;
		info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	}
	else
	{
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	if ((create_info.misc & IMAGE_MISC_EXTERNAL_MEMORY_BIT) != 0)
	{
		if (info.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
		{
			LOGE("Cannot use non-undefined initial layout for external memory.\n");
			return {};
		}

		if (create_info.external.memory_handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
			info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
	}

	info.usage = create_info.usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (create_info.domain == ImageDomain::Transient)
		info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	info.flags = create_info.flags;

	if (info.mipLevels == 0)
		info.mipLevels = image_num_miplevels(info.extent);

	VkImageFormatListCreateInfo format_info = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO };
	VkFormat view_formats[2];
	format_info.pViewFormats = view_formats;
	format_info.viewFormatCount = 2;
	bool create_unorm_srgb_views = false;

	if (create_info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT)
	{
		format_info.viewFormatCount = ImageCreateInfo::compute_view_formats(create_info, view_formats);
		if (format_info.viewFormatCount != 0)
		{
			create_unorm_srgb_views = true;

			const auto *input_format_list = static_cast<const VkBaseInStructure *>(info.pNext);
			while (input_format_list && input_format_list->sType != VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)
				input_format_list = static_cast<const VkBaseInStructure *>(input_format_list->pNext);

			if (ext.supports_image_format_list && !input_format_list)
			{
				format_info.pNext = info.pNext;
				info.pNext = &format_info;
			}
		}
	}

	if ((create_info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT) != 0)
		info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

	uint32_t sharing_indices[QUEUE_INDEX_COUNT];

	uint32_t queue_flags = create_info.misc & (IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
	                                           IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
	                                           IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT |
	                                           IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_DUPLEX);
	bool concurrent_queue = queue_flags != 0 ||
	                        staging_buffer != nullptr ||
	                        create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED;

	if (concurrent_queue)
	{
		info.sharingMode = VK_SHARING_MODE_CONCURRENT;

		// If we didn't specify queue usage,
		// just enable every queue since we need to use transfer queue for initial upload.
		if (staging_buffer && queue_flags == 0)
		{
			// We never imply video here.
			constexpr ImageMiscFlags implicit_queues_all =
					IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
					IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
					IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT;

			queue_flags |= implicit_queues_all;
		}
		else if (staging_buffer)
		{
			// Make sure that these queues are included.
			queue_flags |= IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT;
			if (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT)
				queue_flags |= IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT;
		}

		struct
		{
			uint32_t flags;
			QueueIndices index;
		} static const mappings[] = {
			{ IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT, QUEUE_INDEX_GRAPHICS },
			{ IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT, QUEUE_INDEX_COMPUTE },
			{ IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT, QUEUE_INDEX_TRANSFER },
			{ IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_DECODE_BIT, QUEUE_INDEX_VIDEO_DECODE },
			{ IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_ENCODE_BIT, QUEUE_INDEX_VIDEO_ENCODE },
		};

		for (auto &m : mappings)
			if ((queue_flags & m.flags) != 0)
				add_unique_family(sharing_indices, info.queueFamilyIndexCount, queue_info.family_indices[m.index]);

		if (info.queueFamilyIndexCount > 1)
			info.pQueueFamilyIndices = sharing_indices;
		else
		{
			info.pQueueFamilyIndices = nullptr;
			info.queueFamilyIndexCount = 0;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}
	}

	if (queue_flags == 0)
		queue_flags |= IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT;

	VkFormatFeatureFlags check_extra_features = 0;
	if ((create_info.misc & IMAGE_MISC_VERIFY_FORMAT_FEATURE_SAMPLED_LINEAR_FILTER_BIT) != 0)
		check_extra_features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

	if (info.tiling == VK_IMAGE_TILING_LINEAR)
	{
		if (staging_buffer)
			return ImageHandle(nullptr);

		// Do some more stringent checks.
		if (info.mipLevels > 1)
			return ImageHandle(nullptr);
		if (info.arrayLayers > 1)
			return ImageHandle(nullptr);
		if (info.imageType != VK_IMAGE_TYPE_2D)
			return ImageHandle(nullptr);
		if (info.samples != VK_SAMPLE_COUNT_1_BIT)
			return ImageHandle(nullptr);

		VkImageFormatProperties2 props = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
		if (!get_image_format_properties(info.format, info.imageType, info.tiling, info.usage, info.flags, info.pNext, &props))
			return ImageHandle(nullptr);

		if (!props.imageFormatProperties.maxArrayLayers ||
		    !props.imageFormatProperties.maxMipLevels ||
		    (info.extent.width > props.imageFormatProperties.maxExtent.width) ||
		    (info.extent.height > props.imageFormatProperties.maxExtent.height) ||
		    (info.extent.depth > props.imageFormatProperties.maxExtent.depth))
		{
			return ImageHandle(nullptr);
		}
	}

	if ((create_info.flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT) == 0 &&
	    (!image_format_is_supported(create_info.format, image_usage_to_features(info.usage) | check_extra_features, info.tiling)))
	{
		LOGE("Format %u is not supported for usage flags!\n", unsigned(create_info.format));
		return ImageHandle(nullptr);
	}

	bool use_external = (create_info.misc & IMAGE_MISC_EXTERNAL_MEMORY_BIT) != 0;
	if (use_external && create_info.domain != ImageDomain::Physical)
	{
		LOGE("Must use physical image domain for external memory images.\n");
		return ImageHandle(nullptr);
	}

	if (use_external && !ext.supports_external)
	{
		LOGE("External memory not supported.\n");
		return ImageHandle(nullptr);
	}

	VkExternalMemoryImageCreateInfo external_info = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
	if (ext.supports_external && use_external)
	{
		// Ensure that the handle type is supported.
		VkImageFormatProperties2 props2 = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
		VkExternalImageFormatProperties external_props =
		    { VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
		VkPhysicalDeviceExternalImageFormatInfo external_format_info =
		    { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO };
		external_format_info.handleType = create_info.external.memory_handle_type;

		VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier_info =
				{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT };

		VkImageFormatListCreateInfo format_list = {};
		if (const auto *list_info = find_pnext<VkImageFormatListCreateInfo>(
				info.pNext, VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO))
		{
			format_list = *list_info;
			format_list.pNext = nullptr;
			external_format_info.pNext = &format_list;
		}

		if (info.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
		{
			modifier_info.pNext = external_format_info.pNext;
			external_format_info.pNext = &modifier_info;

			auto *drm_info = find_pnext<VkImageDrmFormatModifierExplicitCreateInfoEXT>(
					info.pNext, VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
			if (!drm_info)
			{
				// There's also the modifier list for export purposes, but we don't care about that yet.
				LOGE("Trying to create DRM modifier image without explicit info.\n");
				return ImageHandle(nullptr);
			}

			modifier_info.drmFormatModifier = drm_info->drmFormatModifier;
			modifier_info.sharingMode = info.sharingMode;
			modifier_info.queueFamilyIndexCount = info.queueFamilyIndexCount;
			modifier_info.pQueueFamilyIndices = info.pQueueFamilyIndices;
		}

		props2.pNext = &external_props;
		if (!get_image_format_properties(info.format, info.imageType, info.tiling,
		                                 info.usage, info.flags,
		                                 &external_format_info, &props2))
		{
			LOGE("Image format is not supported for external memory type #%x.\n",
			     external_format_info.handleType);
			return ImageHandle(nullptr);
		}

		bool supports_import = (external_props.externalMemoryProperties.externalMemoryFeatures &
		                        VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
		bool supports_export = (external_props.externalMemoryProperties.externalMemoryFeatures &
		                        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;

		if (!supports_import && create_info.external)
		{
			LOGE("Attempting to import with handle type #%x, but it is not supported.\n",
			     create_info.external.memory_handle_type);
			return ImageHandle(nullptr);
		}
		else if (!supports_export && !create_info.external)
		{
			LOGE("Attempting to export with handle type #%x, but it is not supported.\n",
			     create_info.external.memory_handle_type);
			return ImageHandle(nullptr);
		}

		external_info.handleTypes = create_info.external.memory_handle_type;
		external_info.pNext = info.pNext;
		info.pNext = &external_info;
	}

	// FIXME: Is there a more intelligent way to detect if we should be using host image copy?
	if (ext.vk14_features.hostImageCopy && staging_buffer && staging_buffer->host.size &&
	    (gpu_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
	     gpu_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU))
	{
		VkHostImageCopyDevicePerformanceQuery query =
				{ VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY };
		VkImageFormatProperties2 props2 = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
		props2.pNext = &query;

		if (get_image_format_properties(info.format, info.imageType, info.tiling,
		                                info.usage | VK_IMAGE_USAGE_HOST_TRANSFER_BIT,
										info.flags, info.pNext, &props2))
		{
			// If we don't lose compression, go ahead.
			if (query.optimalDeviceAccess)
				info.usage |= VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
		}
	}

	bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;
	if (staging_buffer && (generate_mips || (info.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT) == 0))
		info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	if (table->vkCreateImage(device, &info, nullptr, &holder.image) != VK_SUCCESS)
	{
		LOGE("Failed to create image in vkCreateImage.\n");
		return ImageHandle(nullptr);
	}

	if (!allocate_image_memory(&holder.allocation, create_info, holder.image, info.tiling, info.usage))
	{
		LOGE("Failed to allocate memory for image.\n");
		return ImageHandle(nullptr);
	}

	auto tmpinfo = create_info;
	tmpinfo.usage = info.usage;
	tmpinfo.flags = info.flags;
	tmpinfo.levels = info.mipLevels;

	bool has_view = (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
	                               image_usage_video_flags)) != 0 &&
	                (create_info.misc & IMAGE_MISC_NO_DEFAULT_VIEWS_BIT) == 0;
	bool create_mip_views = info.mipLevels > 1 && (create_info.misc & IMAGE_MISC_CREATE_PER_MIP_LEVEL_VIEWS_BIT) != 0;

	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	if (has_view)
	{
		if (!holder.create_default_views(tmpinfo, nullptr, create_info.ycbcr_conversion,
		                                 create_unorm_srgb_views, create_mip_views, view_formats))
		{
			return ImageHandle(nullptr);
		}
		view_type = holder.get_default_view_type();
	}

	ImageHandle handle(handle_pool.images.allocate(this, holder.image, holder.image_view, holder.allocation, tmpinfo, view_type));
	if (handle)
	{
		holder.owned = false;
		if (has_view)
		{
			handle->get_view().set_alt_views(holder.depth_view, holder.stencil_view);
			handle->get_view().set_render_target_views(holder.rt_views);
			handle->get_view().set_mip_views(holder.mip_views);
			handle->get_view().set_unorm_view(holder.unorm_view);
			handle->get_view().set_srgb_view(holder.srgb_view);
			handle->get_view().rebuild_cached_descriptor_payloads(handle->get_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		}
	}

	CommandBufferHandle transition_cmd;

	// Copy initial data to texture.
	if (staging_buffer)
	{
		auto *buffer = staging_buffer->buffer.get();

		// TODO: If we have host image copy, we can bypass this whole thing.
		BufferHandle scratch_buffer;
		if (!buffer && (info.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT) == 0)
		{
			if (staging_buffer->host.size == 0)
			{
				LOGE("Must specifiy either host scratch or buffer.\n");
				return ImageHandle(nullptr);
			}

			BufferCreateInfo scratch_info = {};
			scratch_info.domain = BufferDomain::Host;
			scratch_info.size = staging_buffer->host.size;
			scratch_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			scratch_buffer = create_buffer(scratch_info, staging_buffer->host.data);
			buffer = scratch_buffer.get();
		}

		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);

		// Now we've used the TRANSFER queue to copy data over to the GPU.
		// For mipmapping, we're now moving over to graphics,
		// the transfer queue is designed for CPU <-> GPU and that's it.
		// For concurrent queue mode, we just need to inject a semaphore.

		CommandBufferHandle transfer_cmd;

		if ((info.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT) == 0)
		{
			transfer_cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);

			transfer_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                            VK_PIPELINE_STAGE_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT,
			                            VK_ACCESS_TRANSFER_WRITE_BIT);

			transfer_cmd->begin_region("copy-image-to-gpu");
			transfer_cmd->copy_buffer_to_image(*handle, *buffer,
			                                   staging_buffer->blits.size(), staging_buffer->blits.data());
			transfer_cmd->end_region();
		}
		else
		{
			VkHostImageLayoutTransitionInfo transition = { VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO };
			transition.image = holder.image;
			transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			transition.subresourceRange = {
				format_to_aspect_mask(info.format),
				0, VK_REMAINING_MIP_LEVELS,
				0, VK_REMAINING_ARRAY_LAYERS,
			};
			table->vkTransitionImageLayout(device, 1, &transition);

			VkCopyMemoryToImageInfo copy = { VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO };
			copy.dstImage = handle->get_image();
			copy.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
			copy.regionCount = staging_buffer->blits.size();
			SmallVector<VkMemoryToImageCopy, 32> copies(copy.regionCount);
			copy.pRegions = copies.data();

			for (uint32_t i = 0; i < copy.regionCount; i++)
			{
				auto &dst = copies[i];
				auto &src = staging_buffer->blits[i];
				dst.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY;
				dst.pHostPointer = static_cast<const uint8_t *>(staging_buffer->host.data) + src.bufferOffset;
				dst.imageSubresource = src.imageSubresource;
				dst.imageOffset = src.imageOffset;
				dst.imageExtent = src.imageExtent;
				dst.memoryRowLength = src.bufferRowLength;
				dst.memoryImageHeight = src.bufferImageHeight;
			}

			// Bang the memory straight into the image without a staging copy.
			table->vkCopyMemoryToImage(device, &copy);
		}

		if (generate_mips)
		{
			auto graphics_cmd = request_command_buffer(CommandBuffer::Type::Generic);
			Semaphore sem;

			if (transfer_cmd)
				submit_and_sync_to_queues(transfer_cmd, 1u << QUEUE_INDEX_GRAPHICS);

			auto src_layout =
					(info.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT) != 0 ?
					VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

			graphics_cmd->begin_region("mipgen");
			graphics_cmd->barrier_prepare_generate_mipmap(*handle, src_layout, VK_PIPELINE_STAGE_NONE, 0, true);
			graphics_cmd->generate_mipmap(*handle);
			graphics_cmd->end_region();

			bool sync_with_graphics = (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT) != 0;

			graphics_cmd->image_barrier(
					*handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					create_info.initial_layout,
					VK_PIPELINE_STAGE_2_BLIT_BIT, 0,
					sync_with_graphics ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_NONE,
					sync_with_graphics ? VK_ACCESS_MEMORY_READ_BIT : VK_ACCESS_NONE);

			transition_cmd = std::move(graphics_cmd);
		}
		else if (transfer_cmd)
		{
			bool sync_with_transfer = (create_info.misc & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT) != 0;

			transfer_cmd->image_barrier(
					*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					create_info.initial_layout,
					VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
					sync_with_transfer ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_NONE,
					sync_with_transfer ? VK_ACCESS_MEMORY_READ_BIT : VK_ACCESS_NONE);

			transition_cmd = std::move(transfer_cmd);
		}
		else
		{
			// With host copies, we should just stay in general layout.
			handle->set_layout(Layout::General);
		}
	}
	else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
	{
		VK_ASSERT(create_info.domain != ImageDomain::Transient);

		// Need to perform the barrier in some command buffer, pick an appropriate one based on supported queues.
		// Pick the most lenient queue first in case we need to transition to a weird layout.
		CommandBuffer::Type type = CommandBuffer::Type::Count;
		if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT)
			type = CommandBuffer::Type::Generic;
		else if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT)
			type = CommandBuffer::Type::AsyncCompute;
		else if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT)
			type = CommandBuffer::Type::AsyncTransfer;
		else if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_DECODE_BIT)
			type = CommandBuffer::Type::VideoDecode;
		else if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_ENCODE_BIT)
			type = CommandBuffer::Type::VideoEncode;
		VK_ASSERT(type != CommandBuffer::Type::Count);

		auto cmd = request_command_buffer(type);
		cmd->image_barrier(*handle, info.initialLayout, create_info.initial_layout,
		                   VK_PIPELINE_STAGE_NONE, 0,
		                   VK_PIPELINE_STAGE_NONE, 0);
		transition_cmd = std::move(cmd);
	}

	// For concurrent queue, make sure that compute, transfer or video decode can see the final image as well.
	if (transition_cmd)
	{
		uint32_t sync_queues = 0;

		// These are implied by default.
		if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT)
			sync_queues |= 1u << QUEUE_INDEX_GRAPHICS;
		if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT)
			sync_queues |= 1u << QUEUE_INDEX_COMPUTE;

		// Do not synchronize transfer/video queues here unless we explicitly asked for it.
		if (create_info.misc & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT)
		{
			// Avoid transfer -> graphics -> transfer cycle, have to flush transfer queue before we introduce dependency.
			if (generate_mips)
			{
				LOCK();
				flush_frame_nolock(QUEUE_INDEX_TRANSFER);
			}
			sync_queues |= 1u << QUEUE_INDEX_TRANSFER;
		}
		if (create_info.misc & IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_DECODE_BIT)
			sync_queues |= 1u << QUEUE_INDEX_VIDEO_DECODE;
		if (create_info.misc & IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_ENCODE_BIT)
			sync_queues |= 1u << QUEUE_INDEX_VIDEO_ENCODE;

		submit_and_sync_to_queues(transition_cmd, sync_queues);
	}

	return handle;
}

const ImmutableSampler *Device::request_immutable_sampler(const SamplerCreateInfo &sampler_info,
                                                          const ImmutableYcbcrConversion *ycbcr)
{
	auto info = Sampler::fill_vk_sampler_info(sampler_info);
	Util::Hasher h;

	h.u32(info.flags);
	h.u32(info.addressModeU);
	h.u32(info.addressModeV);
	h.u32(info.addressModeW);
	h.u32(info.minFilter);
	h.u32(info.magFilter);
	h.u32(info.mipmapMode);
	h.f32(info.minLod);
	h.f32(info.maxLod);
	h.f32(info.mipLodBias);
	h.u32(info.compareEnable);
	h.u32(info.compareOp);
	h.u32(info.anisotropyEnable);
	h.f32(info.maxAnisotropy);
	h.u32(info.borderColor);
	h.u32(info.unnormalizedCoordinates);
	if (ycbcr)
		h.u64(ycbcr->get_hash());
	else
		h.u32(0);

	LOCK_CACHE();
	auto *sampler = immutable_samplers.find(h.get());
	if (!sampler)
		sampler = immutable_samplers.emplace_yield(h.get(), h.get(), this, sampler_info, ycbcr);

	return sampler;
}

const ImmutableYcbcrConversion *Device::request_immutable_ycbcr_conversion(
		const VkSamplerYcbcrConversionCreateInfo &info)
{
	Util::Hasher h;
	h.u32(info.forceExplicitReconstruction);
	h.u32(info.format);
	h.u32(info.chromaFilter);
	h.u32(info.components.r);
	h.u32(info.components.g);
	h.u32(info.components.b);
	h.u32(info.components.a);
	h.u32(info.xChromaOffset);
	h.u32(info.yChromaOffset);
	h.u32(info.ycbcrModel);
	h.u32(info.ycbcrRange);

	LOCK_CACHE();
	auto *sampler = immutable_ycbcr_conversions.find(h.get());
	if (!sampler)
		sampler = immutable_ycbcr_conversions.emplace_yield(h.get(), h.get(), this, info);
	return sampler;
}

SamplerHandle Device::create_sampler(const SamplerCreateInfo &sampler_info)
{
	auto info = Sampler::fill_vk_sampler_info(sampler_info);
	VkSampler sampler;
	if (table->vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
		return SamplerHandle(nullptr);
	return SamplerHandle(handle_pool.samplers.allocate(this, sampler, sampler_info, false));
}

BindlessDescriptorPoolHandle Device::create_bindless_descriptor_pool(BindlessResourceType type,
                                                                     unsigned num_sets, unsigned num_descriptors)
{
	if (!ext.vk12_features.descriptorIndexing)
		return BindlessDescriptorPoolHandle{nullptr};

	DescriptorSetLayout layout;
	const uint32_t stages_for_sets[VULKAN_NUM_BINDINGS] = { VK_SHADER_STAGE_ALL };
	layout.array_size[0] = DescriptorSetLayout::UNSIZED_ARRAY;
	for (unsigned i = 1; i < VULKAN_NUM_BINDINGS; i++)
		layout.array_size[i] = 1;

	switch (type)
	{
	case BindlessResourceType::Image:
		layout.separate_image_mask = 1;
		break;

	default:
		return BindlessDescriptorPoolHandle{nullptr};
	}

	auto *allocator = request_descriptor_set_allocator(layout, stages_for_sets, nullptr);

	VkDescriptorPool pool = VK_NULL_HANDLE;

	if (!ext.supports_descriptor_buffer)
	{
		if (allocator)
			pool = allocator->allocate_bindless_pool(num_sets, num_descriptors);

		if (!pool)
		{
			LOGE("Failed to allocate bindless pool.\n");
			return BindlessDescriptorPoolHandle{nullptr};
		}
	}

	auto *handle = handle_pool.bindless_descriptor_pool.allocate(this, allocator, pool,
	                                                             num_sets, num_descriptors);
	return BindlessDescriptorPoolHandle{handle};
}

void Device::fill_buffer_sharing_indices(VkBufferCreateInfo &info, uint32_t *sharing_indices)
{
	for (auto &i : queue_info.family_indices)
		add_unique_family(sharing_indices, info.queueFamilyIndexCount, i);

	if (info.queueFamilyIndexCount > 1)
	{
		info.sharingMode = VK_SHARING_MODE_CONCURRENT;
		info.pQueueFamilyIndices = sharing_indices;
	}
	else
	{
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.queueFamilyIndexCount = 0;
		info.pQueueFamilyIndices = nullptr;
	}
}

BufferHandle Device::create_imported_host_buffer(const BufferCreateInfo &create_info, VkExternalMemoryHandleTypeFlagBits type, void *host_buffer)
{
	if (create_info.domain != BufferDomain::Host &&
	    create_info.domain != BufferDomain::CachedHost &&
	    create_info.domain != BufferDomain::CachedCoherentHostPreferCached &&
	    create_info.domain != BufferDomain::CachedCoherentHostPreferCoherent)
	{
		return BufferHandle{};
	}

	if (!ext.supports_external_memory_host)
		return BufferHandle{};

	if ((reinterpret_cast<uintptr_t>(host_buffer) & (ext.host_memory_properties.minImportedHostPointerAlignment - 1)) != 0)
	{
		LOGE("Host buffer is not aligned appropriately.\n");
		return BufferHandle{};
	}

	VkExternalMemoryBufferCreateInfo external_info = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
	external_info.handleTypes = type;

	VkMemoryHostPointerPropertiesEXT host_pointer_props = { VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT };
	if (table->vkGetMemoryHostPointerPropertiesEXT(device, type, host_buffer, &host_pointer_props) != VK_SUCCESS)
	{
		LOGE("Host pointer is not importable.\n");
		return BufferHandle{};
	}

	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	VkBufferUsageFlags2CreateInfo usage2 = { VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO };
	info.size = create_info.size;
	usage2.usage = create_info.usage;
	if (get_device_features().vk12_features.bufferDeviceAddress)
		usage2.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.pNext = &external_info;

	external_info.pNext = create_info.pnext;

	uint32_t sharing_indices[QUEUE_INDEX_COUNT];
	fill_buffer_sharing_indices(info, sharing_indices);

	if (ext.vk14_features.maintenance5)
	{
		usage2.pNext = info.pNext;
		info.pNext = &usage2;
	}
	else
		info.usage = VkBufferUsageFlags(usage2.usage);

	VkBuffer buffer;
	VkMemoryRequirements reqs;
	if (table->vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return BufferHandle{};

	table->vkGetBufferMemoryRequirements(device, buffer, &reqs);

	reqs.alignment = std::max<uint32_t>(reqs.alignment, gpu_props.limits.nonCoherentAtomSize);
	// For BDA purposes
	reqs.alignment = std::max<uint32_t>(reqs.alignment, 16u);

	// Weird workaround for latest AMD Windows drivers which sets memoryTypeBits to 0 when using the external handle type.
	if (!reqs.memoryTypeBits)
		reqs.memoryTypeBits = ~0u;

	auto plain_reqs = reqs;
	reqs.memoryTypeBits &= host_pointer_props.memoryTypeBits;

	if (reqs.memoryTypeBits == 0)
	{
		LOGE("No compatible host pointer types are available.\n");
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);

	if (memory_type == UINT32_MAX)
	{
		// Weird workaround for Intel Windows where the only memory type is DEVICE_LOCAL
		// with no HOST_VISIBLE (!?!?!).
		// However, it appears to work just fine to allocate with other memory types as well ...
		// Oh well.

		// Ignore host_pointer_props.
		reqs = plain_reqs;
		memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	}

	if (memory_type == UINT32_MAX)
	{
		LOGE("Failed to find memory type.\n");
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = (create_info.size + ext.host_memory_properties.minImportedHostPointerAlignment - 1) &
	                            ~(ext.host_memory_properties.minImportedHostPointerAlignment - 1);
	alloc_info.memoryTypeIndex = memory_type;

	VkMemoryAllocateFlagsInfo flags_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
	if (get_device_features().vk12_features.bufferDeviceAddress)
	{
		alloc_info.pNext = &flags_info;
		flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	}

	VkImportMemoryHostPointerInfoEXT import = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT };
	import.handleType = type;
	import.pHostPointer = host_buffer;
	import.pNext = alloc_info.pNext;
	alloc_info.pNext = &import;

	VkDeviceMemory memory;
	if (table->vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS)
	{
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	auto allocation = DeviceAllocation::make_imported_allocation(memory, info.size, memory_type);
	if (table->vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void **>(&allocation.host_base)) != VK_SUCCESS)
	{
		{
			LOCK_MEMORY();
			allocation.free_immediate(managers.memory);
		}
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	if (table->vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS)
	{
		{
			LOCK_MEMORY();
			allocation.free_immediate(managers.memory);
		}
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	VkDeviceAddress bda = 0;
	if (get_device_features().vk12_features.bufferDeviceAddress)
	{
		VkBufferDeviceAddressInfo bda_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		bda_info.buffer = buffer;
		bda = table->vkGetBufferDeviceAddress(device, &bda_info);
	}

	BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, create_info, bda));
	return handle;
}

BufferHandle Device::create_buffer(const BufferCreateInfo &create_info, const void *initial)
{
	DeviceAllocation allocation;
	VkBuffer buffer;

	bool zero_initialize = (create_info.misc & BUFFER_MISC_ZERO_INITIALIZE_BIT) != 0;
	bool use_external = (create_info.misc & BUFFER_MISC_EXTERNAL_MEMORY_BIT) != 0;
	if (initial && zero_initialize)
	{
		LOGE("Cannot initialize buffer with data and clear.\n");
		return BufferHandle{};
	}

	if (use_external && create_info.domain != BufferDomain::Device)
	{
		LOGE("When using external memory, must be Device domain.\n");
		return BufferHandle{};
	}

	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	VkBufferUsageFlags2CreateInfo usage2 = { VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO };
	info.size = create_info.size;
	usage2.usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (get_device_features().vk12_features.bufferDeviceAddress)
		usage2.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.pNext = create_info.pnext;

	uint32_t sharing_indices[QUEUE_INDEX_COUNT];
	fill_buffer_sharing_indices(info, sharing_indices);

	if (use_external && !ext.supports_external)
	{
		LOGE("External memory not supported.\n");
		return BufferHandle{};
	}

	VkExternalMemoryBufferCreateInfo external_info = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
	if (ext.supports_external && use_external)
	{
		// Ensure that the handle type is supported.
		VkPhysicalDeviceExternalBufferInfo external_buffer_props_info =
		    { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO };
		VkExternalBufferProperties external_buffer_props = { VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES };
		external_buffer_props_info.handleType = create_info.external.memory_handle_type;
		external_buffer_props_info.usage = VkBufferUsageFlags(usage2.usage);
		external_buffer_props_info.flags = info.flags;
		vkGetPhysicalDeviceExternalBufferProperties(gpu, &external_buffer_props_info, &external_buffer_props);

		bool supports_import = (external_buffer_props.externalMemoryProperties.externalMemoryFeatures &
		                        VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
		bool supports_export = (external_buffer_props.externalMemoryProperties.externalMemoryFeatures &
		                        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;

		if (!supports_import && !create_info.external)
		{
			LOGE("Attempting to import with handle type #%x, but it is not supported.\n",
			     create_info.external.memory_handle_type);
			return BufferHandle{};
		}
		else if (!supports_export && create_info.external)
		{
			LOGE("Attempting to export with handle type #%x, but it is not supported.\n",
			     create_info.external.memory_handle_type);
			return BufferHandle{};
		}

		external_info.handleTypes = create_info.external.memory_handle_type;
		external_info.pNext = info.pNext;
		info.pNext = &external_info;
	}

	if (ext.vk14_features.maintenance5)
	{
		usage2.pNext = info.pNext;
		info.pNext = &usage2;
	}
	else
		info.usage = VkBufferUsageFlags(usage2.usage);

	if (table->vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return BufferHandle(nullptr);

	VkMemoryRequirements2 reqs = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
	VkBufferMemoryRequirementsInfo2 req_info = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
	req_info.buffer = buffer;
	table->vkGetBufferMemoryRequirements2(device, &req_info, &reqs);

	reqs.memoryRequirements.alignment = std::max<uint32_t>(reqs.memoryRequirements.alignment, gpu_props.limits.nonCoherentAtomSize);
	// For BDA purposes
	reqs.memoryRequirements.alignment = std::max<uint32_t>(reqs.memoryRequirements.alignment, 16u);

	if (create_info.allocation_requirements.size)
	{
		reqs.memoryRequirements.memoryTypeBits &=
				create_info.allocation_requirements.memoryTypeBits;
		reqs.memoryRequirements.size =
				std::max<VkDeviceSize>(reqs.memoryRequirements.size, create_info.allocation_requirements.size);
		reqs.memoryRequirements.alignment =
				std::max<VkDeviceSize>(reqs.memoryRequirements.alignment, create_info.allocation_requirements.alignment);
	}

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryRequirements.memoryTypeBits);
	if (memory_type == UINT32_MAX)
	{
		LOGE("Failed to find memory type.\n");
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	AllocationMode mode;
	if ((create_info.misc & BUFFER_MISC_EXTERNAL_MEMORY_BIT) != 0)
		mode = AllocationMode::External;
	else if (create_info.domain == BufferDomain::Device &&
	    (create_info.usage & (VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) != 0)
		mode = AllocationMode::LinearDeviceHighPriority;
	else if (create_info.domain == BufferDomain::Device ||
	         create_info.domain == BufferDomain::LinkedDeviceHostPreferDevice)
		mode = AllocationMode::LinearDevice;
	else
		mode = AllocationMode::LinearHostMappable;

	auto external = create_info.external;

	{
		LOCK_MEMORY();
		if (!managers.memory.allocate_buffer_memory(reqs.memoryRequirements.size, reqs.memoryRequirements.alignment,
		                                            mode, memory_type, buffer, &allocation,
		                                            use_external ? &external : nullptr))
		{
			if (use_external)
			{
				LOGE("Failed to export / import buffer memory.\n");
				table->vkDestroyBuffer(device, buffer, nullptr);
				return BufferHandle(nullptr);
			}

			auto fallback_domain = create_info.domain;

			// This memory type is rather scarce, so fallback to Host type if we've exhausted this memory.
			if (create_info.domain == BufferDomain::LinkedDeviceHost)
			{
				LOGW("Exhausted LinkedDeviceHost memory, falling back to host.\n");
				fallback_domain = BufferDomain::Host;
			}
			else if (create_info.domain == BufferDomain::LinkedDeviceHostPreferDevice)
			{
				LOGW("Exhausted LinkedDeviceHostPreferDevice memory, falling back to device.\n");
				fallback_domain = BufferDomain::Device;
			}

			memory_type = find_memory_type(fallback_domain, reqs.memoryRequirements.memoryTypeBits);

			if (memory_type == UINT32_MAX || fallback_domain == create_info.domain ||
			    !managers.memory.allocate_buffer_memory(reqs.memoryRequirements.size, reqs.memoryRequirements.alignment,
			                                            mode, memory_type, buffer, &allocation, nullptr))
			{
				LOGE("Failed to allocate fallback memory.\n");
				table->vkDestroyBuffer(device, buffer, nullptr);
				return BufferHandle(nullptr);
			}
		}
	}

	if (table->vkBindBufferMemory(device, buffer, allocation.get_memory(), allocation.get_offset()) != VK_SUCCESS)
	{
		{
			LOCK_MEMORY();
			allocation.free_immediate(managers.memory);
		}

		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	auto tmpinfo = create_info;
	tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VkDeviceAddress bda = 0;
	if (get_device_features().vk12_features.bufferDeviceAddress)
	{
		VkBufferDeviceAddressInfo bda_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		bda_info.buffer = buffer;
		bda = table->vkGetBufferDeviceAddress(device, &bda_info);
	}

	BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, tmpinfo, bda));

	bool need_init = initial || zero_initialize;
	void *ptr = nullptr;
	if (need_init && memory_type_is_host_visible(memory_type))
		ptr = managers.memory.map_memory(allocation, MEMORY_ACCESS_WRITE_BIT, 0, allocation.get_size());

	if (need_init && !ptr)
	{
		auto cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		if (initial)
		{
			auto staging_info = create_info;
			staging_info.domain = BufferDomain::Host;
			auto staging_buffer = create_buffer(staging_info, initial);
			set_name(*staging_buffer, "buffer-upload-staging-buffer");

			cmd->begin_region("copy-buffer-staging");
			cmd->copy_buffer(*handle, *staging_buffer);
			cmd->end_region();
		}
		else
		{
			cmd->begin_region("fill-buffer-staging");
			cmd->fill_buffer(*handle, 0);
			cmd->end_region();
		}

		uint32_t queue_indices = (1u << QUEUE_INDEX_GRAPHICS) | (1u << QUEUE_INDEX_COMPUTE);
		if (ext.supports_video_decode_queue)
			queue_indices |= 1u << QUEUE_INDEX_VIDEO_DECODE;
		if (ext.supports_video_encode_queue)
			queue_indices |= 1u << QUEUE_INDEX_VIDEO_ENCODE;
		submit_and_sync_to_queues(cmd, queue_indices);
	}
	else if (need_init)
	{
		if (initial)
			memcpy(ptr, initial, create_info.size);
		else
			memset(ptr, 0, create_info.size);
		managers.memory.unmap_memory(allocation, MEMORY_ACCESS_WRITE_BIT, 0, allocation.get_size());
	}
	return handle;
}

bool Device::memory_type_is_device_optimal(uint32_t type) const
{
	return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
}

bool Device::memory_type_is_host_visible(uint32_t type) const
{
	return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

static VkFormatFeatureFlags2 promote_storage_usage(const DeviceFeatures &features, VkFormat format,
                                                   VkFormatFeatureFlags2 supported)
{
	if ((supported & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) != 0 &&
	    format_supports_storage_image_read_write_without_format(format))
	{
		if (features.enabled_features.shaderStorageImageReadWithoutFormat)
			supported |= VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT;
		if (features.enabled_features.shaderStorageImageWriteWithoutFormat)
			supported |= VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
	}

	return supported;
}

void Device::get_format_properties(VkFormat format, VkFormatProperties3 *properties3) const
{
	VkFormatProperties2 properties2 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
	VK_ASSERT(properties3->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3);

	if (ext.supports_format_feature_flags2)
	{
		properties2.pNext = properties3;
		vkGetPhysicalDeviceFormatProperties2(gpu, format, &properties2);
	}
	else
	{
		// Skip properties3 and synthesize the results instead.
		properties2.pNext = properties3->pNext;
		vkGetPhysicalDeviceFormatProperties2(gpu, format, &properties2);

		properties3->optimalTilingFeatures = properties2.formatProperties.optimalTilingFeatures;
		properties3->linearTilingFeatures = properties2.formatProperties.linearTilingFeatures;
		properties3->bufferFeatures = properties2.formatProperties.bufferFeatures;

		// Automatically promote for supported formats.
		properties3->optimalTilingFeatures =
				promote_storage_usage(ext, format, properties3->optimalTilingFeatures);
		properties3->linearTilingFeatures =
				promote_storage_usage(ext, format, properties3->linearTilingFeatures);
	}
}

bool Device::get_image_format_properties(VkFormat format, VkImageType type, VkImageTiling tiling,
                                         VkImageUsageFlags usage, VkImageCreateFlags flags,
                                         const void *pNext,
                                         VkImageFormatProperties2 *properties2) const
{
	VK_ASSERT(properties2->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);
	VkPhysicalDeviceImageFormatInfo2 info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 };
	info.pNext = pNext;
	info.format = format;
	info.type = type;
	info.tiling = tiling;
	info.usage = usage;
	info.flags = flags;

	VkResult res = vkGetPhysicalDeviceImageFormatProperties2(gpu, &info, properties2);
	return res == VK_SUCCESS;
}

bool Device::image_format_is_supported(VkFormat format, VkFormatFeatureFlags2 required, VkImageTiling tiling) const
{
	VkFormatProperties3 props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
	get_format_properties(format, &props3);
	auto flags = tiling == VK_IMAGE_TILING_OPTIMAL ? props3.optimalTilingFeatures : props3.linearTilingFeatures;
	return (flags & required) == required;
}

VkFormat Device::get_default_depth_stencil_format() const
{
	if (image_format_is_supported(VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
		return VK_FORMAT_D24_UNORM_S8_UINT;
	if (image_format_is_supported(VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
		return VK_FORMAT_D32_SFLOAT_S8_UINT;

	return VK_FORMAT_UNDEFINED;
}

VkFormat Device::get_default_depth_format() const
{
	if (image_format_is_supported(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
		return VK_FORMAT_D32_SFLOAT;
	if (image_format_is_supported(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
		return VK_FORMAT_X8_D24_UNORM_PACK32;
	if (image_format_is_supported(VK_FORMAT_D16_UNORM, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
		return VK_FORMAT_D16_UNORM;

	return VK_FORMAT_UNDEFINED;
}

uint64_t Device::allocate_cookie()
{
	// Reserve lower bits for "special purposes".
	return cookie.fetch_add(32, std::memory_order_relaxed) + 32;
}

const RenderPass &Device::request_render_pass(const RenderPassInfo &info, bool compatible)
{
	Hasher h;
	VkFormat formats[VULKAN_NUM_ATTACHMENTS];
	VkFormat depth_stencil;
	uint32_t lazy = 0;
	uint32_t optimal = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		VK_ASSERT(info.color_attachments[i]);
		formats[i] = info.color_attachments[i]->get_format();
		if (info.color_attachments[i]->get_image().get_create_info().domain == ImageDomain::Transient)
			lazy |= 1u << i;
		if (info.color_attachments[i]->get_image().get_layout_type() == Layout::Optimal)
			optimal |= 1u << i;

		// This can change external subpass dependencies, so it must always be hashed.
		h.u32(info.color_attachments[i]->get_image().get_swapchain_layout());
	}

	if (info.depth_stencil)
	{
		if (info.depth_stencil->get_image().get_create_info().domain == ImageDomain::Transient)
			lazy |= 1u << info.num_color_attachments;
		if (info.depth_stencil->get_image().get_layout_type() == Layout::Optimal)
			optimal |= 1u << info.num_color_attachments;
	}

	// For multiview, base layer is encoded into the view mask.
	if (info.num_layers > 1)
	{
		h.u32(info.base_layer);
		h.u32(info.num_layers);
	}
	else
	{
		h.u32(0);
		h.u32(info.num_layers);
	}

	h.u32(info.num_subpasses);
	for (unsigned i = 0; i < info.num_subpasses; i++)
	{
		h.u32(info.subpasses[i].num_color_attachments);
		h.u32(info.subpasses[i].num_input_attachments);
		h.u32(info.subpasses[i].num_resolve_attachments);
		h.u32(static_cast<uint32_t>(info.subpasses[i].depth_stencil_mode));
		for (unsigned j = 0; j < info.subpasses[i].num_color_attachments; j++)
			h.u32(info.subpasses[i].color_attachments[j]);
		for (unsigned j = 0; j < info.subpasses[i].num_input_attachments; j++)
			h.u32(info.subpasses[i].input_attachments[j]);
		for (unsigned j = 0; j < info.subpasses[i].num_resolve_attachments; j++)
			h.u32(info.subpasses[i].resolve_attachments[j]);
	}

	depth_stencil = info.depth_stencil ? info.depth_stencil->get_format() : VK_FORMAT_UNDEFINED;
	h.data(formats, info.num_color_attachments * sizeof(VkFormat));
	h.u32(info.num_color_attachments);
	h.u32(depth_stencil);

	// Compatible render passes do not care about load/store, or image layouts.
	if (!compatible)
	{
		h.u32(info.op_flags);
		h.u32(info.clear_attachments);
		h.u32(info.load_attachments);
		h.u32(info.store_attachments);
		h.u32(optimal);
	}

	// Lazy flag can change external subpass dependencies, which is not compatible.
	h.u32(lazy);

	// Marked for v2 render passes.
	h.u32(2);

	auto hash = h.get();

	auto *ret = render_passes.find(hash);
	if (!ret)
		ret = render_passes.emplace_yield(hash, hash, this, info);
	return *ret;
}

const Framebuffer &Device::request_framebuffer(const RenderPassInfo &info)
{
	return framebuffer_allocator.request_framebuffer(info);
}

ImageHandle Device::get_transient_attachment(unsigned width, unsigned height, VkFormat format,
                                             unsigned index, unsigned samples, unsigned layers)
{
	return transient_allocator.request_attachment(width, height, format, index, samples, layers);
}

ImageView &Device::get_swapchain_view()
{
	VK_ASSERT(wsi.index < wsi.swapchain.size());
	return wsi.swapchain[wsi.index]->get_view();
}

ImageView &Device::get_swapchain_view(unsigned index)
{
	VK_ASSERT(index < wsi.swapchain.size());
	return wsi.swapchain[index]->get_view();
}

unsigned Device::get_num_frame_contexts() const
{
	return unsigned(per_frame.size());
}

unsigned Device::get_num_swapchain_images() const
{
	return unsigned(wsi.swapchain.size());
}

unsigned Device::get_swapchain_index() const
{
	return wsi.index;
}

unsigned Device::get_current_frame_context() const
{
	return frame_context_index;
}

RenderPassInfo Device::get_swapchain_render_pass(SwapchainRenderPass style)
{
	RenderPassInfo info;
	info.num_color_attachments = 1;
	info.color_attachments[0] = &get_swapchain_view();
	info.clear_attachments = ~0u;
	info.store_attachments = 1u << 0;

	switch (style)
	{
	case SwapchainRenderPass::Depth:
	{
		info.op_flags |= RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
		auto att = get_transient_attachment(wsi.swapchain[wsi.index]->get_create_info().width,
		                                    wsi.swapchain[wsi.index]->get_create_info().height,
		                                    get_default_depth_format());
		info.depth_stencil = &att->get_view();
		break;
	}

	case SwapchainRenderPass::DepthStencil:
	{
		info.op_flags |= RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
		auto att = get_transient_attachment(wsi.swapchain[wsi.index]->get_create_info().width,
		                                    wsi.swapchain[wsi.index]->get_create_info().height,
		                                    get_default_depth_stencil_format());
		info.depth_stencil = &att->get_view();
		break;
	}

	default:
		break;
	}
	return info;
}

void Device::external_queue_lock()
{
	lock.lock.lock();
	if (queue_lock_callback)
		queue_lock_callback();
}

void Device::external_queue_unlock()
{
	lock.lock.unlock();
	if (queue_unlock_callback)
		queue_unlock_callback();
}

void Device::set_queue_lock(std::function<void()> lock_callback, std::function<void()> unlock_callback)
{
	queue_lock_callback = std::move(lock_callback);
	queue_unlock_callback = std::move(unlock_callback);
}

void Device::set_name(uint64_t object, VkObjectType type, const char *name)
{
	if (ext.supports_debug_utils)
	{
		VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = type;
		info.objectHandle = object;
		info.pObjectName = name;
		// Be defensive against broken loaders (Android have been weird here in the past).
		if (vkSetDebugUtilsObjectNameEXT)
			vkSetDebugUtilsObjectNameEXT(device, &info);
	}
}

void Device::set_name(const Buffer &buffer, const char *name)
{
	set_name((uint64_t)buffer.get_buffer(), VK_OBJECT_TYPE_BUFFER, name);
}

void Device::set_name(const Image &image, const char *name)
{
	set_name((uint64_t)image.get_image(), VK_OBJECT_TYPE_IMAGE, name);
}

void Device::set_name(const CommandBuffer &cmd, const char *name)
{
	set_name((uint64_t)cmd.get_command_buffer(), VK_OBJECT_TYPE_COMMAND_BUFFER, name);
}

void Device::query_available_performance_counters(CommandBuffer::Type type, uint32_t *count,
                                                  const VkPerformanceCounterKHR **counters,
                                                  const VkPerformanceCounterDescriptionKHR **desc)
{
	auto &query_pool = get_performance_query_pool(get_physical_queue_type(type));
	*count = query_pool.get_num_counters();
	*counters = query_pool.get_available_counters();
	*desc = query_pool.get_available_counter_descs();
}

bool Device::init_performance_counters(CommandBuffer::Type type, const std::vector<std::string> &names)
{
	return queue_data[get_physical_queue_type(type)].performance_query_pool.init_counters(names);
}

void Device::release_profiling()
{
	table->vkReleaseProfilingLockKHR(device);
}

bool Device::acquire_profiling()
{
	if (!ext.performance_query_features.performanceCounterQueryPools)
		return false;

	VkAcquireProfilingLockInfoKHR info = { VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR };
	info.timeout = UINT64_MAX;
	if (table->vkAcquireProfilingLockKHR(device, &info) != VK_SUCCESS)
	{
		LOGE("Failed to acquire profiling lock.\n");
		return false;
	}

	return true;
}

void Device::add_debug_channel_buffer(DebugChannelInterface *iface, std::string tag, Vulkan::BufferHandle buffer)
{
	buffer->set_internal_sync_object();
	LOCK();
	frame().debug_channels.push_back({ iface, std::move(tag), std::move(buffer) });
}

void Device::parse_debug_channel(const PerFrame::DebugChannel &channel)
{
	if (!channel.iface)
		return;

	auto *words = static_cast<const DebugChannelInterface::Word *>(map_host_buffer(*channel.buffer, MEMORY_ACCESS_READ_BIT));

	size_t size = channel.buffer->get_create_info().size;
	if (size <= sizeof(uint32_t))
	{
		LOGE("Debug channel buffer is too small.\n");
		return;
	}

	// Format for the debug channel.
	// Word 0: Atomic counter used by shader.
	// Word 1-*: [total message length, code, x, y, z, args]

	size -= sizeof(uint32_t);
	size /= sizeof(uint32_t);

	if (words[0].u32 > size)
	{
		LOGW("Debug channel overflowed and messaged were dropped. Consider increasing debug channel size to at least %u bytes.\n",
		     unsigned((words[0].u32 + 1) * sizeof(uint32_t)));
	}

	words++;

	while (size != 0 && words[0].u32 >= 5 && words[0].u32 <= size)
	{
		channel.iface->message(channel.tag, words[1].u32,
		                       words[2].u32, words[3].u32, words[4].u32,
		                       words[0].u32 - 5, &words[5]);
		size -= words[0].u32;
		words += words[0].u32;
	}

	unmap_host_buffer(*channel.buffer, MEMORY_ACCESS_READ_BIT);
}

static int64_t convert_to_signed_delta(uint64_t start_ticks, uint64_t end_ticks, unsigned valid_bits)
{
	unsigned shamt = 64 - valid_bits;
	start_ticks <<= shamt;
	end_ticks <<= shamt;
	auto ticks_delta = int64_t(end_ticks - start_ticks);
	ticks_delta >>= shamt;
	return ticks_delta;
}

double Device::convert_device_timestamp_delta(uint64_t start_ticks, uint64_t end_ticks) const
{
	int64_t ticks_delta = convert_to_signed_delta(start_ticks, end_ticks, queue_info.timestamp_valid_bits);
	return double(int64_t(ticks_delta)) * gpu_props.limits.timestampPeriod * 1e-9;
}

uint64_t Device::update_wrapped_device_timestamp(uint64_t ts)
{
	calibrated_timestamp_device_accum +=
			convert_to_signed_delta(calibrated_timestamp_device_accum,
			                        ts,
			                        queue_info.timestamp_valid_bits);
	return calibrated_timestamp_device_accum;
}

int64_t Device::convert_timestamp_to_absolute_nsec(const QueryPoolResult &handle)
{
	auto ts = int64_t(handle.get_timestamp_ticks());
	if (handle.is_device_timebase())
	{
		// Ensure that we deal with timestamp wraparound correctly.
		// On some hardware, we have < 64 valid bits and the timestamp counters will wrap around at some interval.
		// As long as timestamps come in at a reasonably steady pace, we can deal with wraparound cleanly.
		ts = update_wrapped_device_timestamp(ts);
		ts = calibrated_timestamp_host + int64_t(double(ts - calibrated_timestamp_device) * gpu_props.limits.timestampPeriod);
	}
	return ts;
}

PipelineEvent Device::begin_signal_event()
{
	return request_pipeline_event();
}

#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
ResourceManager &Device::get_resource_manager()
{
	return resource_manager;
}

ShaderManager &Device::get_shader_manager()
{
#ifdef GRANITE_VULKAN_FOSSILIZE
	if (query_initialization_progress(InitializationStage::ShaderModules) < 100)
	{
		LOGW("Querying shader manager before completion of module initialization.\n"
		     "Application should not hit this case.\n"
		     "Blocking until completion ... Try using DeviceShaderModuleReadyEvent or PipelineReadyEvent instead.\n");
		block_until_shader_module_ready();
	}
#endif
	return shader_manager;
}
#endif

#ifdef GRANITE_VULKAN_SYSTEM_HANDLES
void Device::init_shader_manager_cache()
{
	if (!shader_manager.load_shader_cache("assets://shader_cache.json"))
		shader_manager.load_shader_cache("cache://shader_cache.json");
}

void Device::flush_shader_manager_cache()
{
	shader_manager.save_shader_cache("cache://shader_cache.json");
}
#endif

const VolkDeviceTable &Device::get_device_table() const
{
	return *table;
}

#ifndef GRANITE_RENDERDOC_CAPTURE
bool Device::init_renderdoc_capture()
{
	LOGE("RenderDoc API capture is not enabled in this build.\n");
	return false;
}

void Device::begin_renderdoc_capture()
{
}

void Device::end_renderdoc_capture()
{
}
#endif

bool Device::supports_subgroup_size_log2(bool subgroup_full_group, uint8_t subgroup_minimum_size_log2,
                                         uint8_t subgroup_maximum_size_log2, VkShaderStageFlagBits stage) const
{
	if (ImplementationQuirks::get().force_no_subgroup_size_control)
		return false;

	if (stage != VK_SHADER_STAGE_COMPUTE_BIT &&
	    stage != VK_SHADER_STAGE_MESH_BIT_EXT &&
	    stage != VK_SHADER_STAGE_TASK_BIT_EXT)
	{
		return false;
	}

	if (!ext.vk13_features.subgroupSizeControl)
		return false;
	if (subgroup_full_group && !ext.vk13_features.computeFullSubgroups)
		return false;

	uint32_t min_subgroups = 1u << subgroup_minimum_size_log2;
	uint32_t max_subgroups = 1u << subgroup_maximum_size_log2;

	bool full_range = min_subgroups <= ext.vk13_props.minSubgroupSize &&
	                  max_subgroups >= ext.vk13_props.maxSubgroupSize;

	// We can use VARYING size.
	if (full_range)
		return true;

	if (min_subgroups > ext.vk13_props.maxSubgroupSize ||
	    max_subgroups < ext.vk13_props.minSubgroupSize)
	{
		// No overlap in requested subgroup size and available subgroup size.
		return false;
	}

	// We need requiredSubgroupSizeStages support here.
	return (ext.vk13_props.requiredSubgroupSizeStages & stage) != 0;
}

const QueueInfo &Device::get_queue_info() const
{
	return queue_info;
}

void Device::timestamp_log_reset()
{
	managers.timestamps.reset();
}

void Device::timestamp_log(const TimestampIntervalReportCallback &cb) const
{
	managers.timestamps.log_simple(cb);
}

CommandBufferHandle request_command_buffer_with_ownership_transfer(
		Device &device,
		const Vulkan::Image &image,
		const OwnershipTransferInfo &info,
		const Vulkan::Semaphore &semaphore)
{
	auto &queue_info = device.get_queue_info();
	unsigned old_family = queue_info.family_indices[device.get_physical_queue_type(info.old_queue)];
	unsigned new_family = queue_info.family_indices[device.get_physical_queue_type(info.new_queue)];
	bool image_is_concurrent = (image.get_create_info().misc &
	                            (Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT |
	                             Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
	                             Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
	                             Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_VIDEO_DUPLEX)) != 0;
	bool need_ownership_transfer = old_family != new_family && !image_is_concurrent;

	VkImageMemoryBarrier2 ownership = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	ownership.image = image.get_image();
	ownership.subresourceRange.aspectMask = format_to_aspect_mask(image.get_format());
	ownership.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	ownership.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	ownership.oldLayout = info.old_image_layout;
	ownership.newLayout = info.new_image_layout;
	ownership.srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

	if (need_ownership_transfer)
	{
		ownership.srcQueueFamilyIndex = old_family;
		ownership.dstQueueFamilyIndex = new_family;

		if (semaphore)
			device.add_wait_semaphore(info.old_queue, semaphore, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, true);
		auto release_cmd = device.request_command_buffer(info.old_queue);

		release_cmd->image_barriers(1, &ownership);

		Semaphore sem;
		device.submit(release_cmd, nullptr, 1, &sem);
		device.add_wait_semaphore(info.new_queue, sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, true);
	}
	else
	{
		ownership.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		ownership.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		if (semaphore)
			device.add_wait_semaphore(info.new_queue, semaphore, info.dst_pipeline_stage, true);
	}

	// Ownership transfers may perform writes, so make those operations visible.
	// If we require neither layout transition nor ownership transfer,
	// visibility is ensured by semaphores.
	bool need_dst_barrier = need_ownership_transfer || info.old_image_layout != info.new_image_layout;

	auto acquire_cmd = device.request_command_buffer(info.new_queue);
	if (need_dst_barrier)
	{
		if (!need_ownership_transfer)
			ownership.srcStageMask = info.dst_pipeline_stage;
		ownership.dstAccessMask = info.dst_access;
		ownership.dstStageMask = info.dst_pipeline_stage;
		acquire_cmd->image_barriers(1, &ownership);
	}

	return acquire_cmd;
}

static ImplementationQuirks implementation_quirks;
ImplementationQuirks &ImplementationQuirks::get()
{
	return implementation_quirks;
}
}
