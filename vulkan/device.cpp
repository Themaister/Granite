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

#include "device.hpp"
#include "format.hpp"
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

#ifdef GRANITE_VULKAN_FILESYSTEM
#include "string_helpers.hpp"
#endif

#ifdef GRANITE_VULKAN_MT
#include "thread_id.hpp"
static unsigned get_thread_index()
{
	return Vulkan::get_current_thread_index();
}
#define LOCK() std::lock_guard<std::mutex> holder__{lock.lock}
#define DRAIN_FRAME_LOCK() \
	std::unique_lock<std::mutex> holder__{lock.lock}; \
	lock.cond.wait(holder__, [&]() { \
		return lock.counter == 0; \
	})
#else
#define LOCK() ((void)0)
#define DRAIN_FRAME_LOCK() VK_ASSERT(lock.counter == 0)
static unsigned get_thread_index()
{
	return 0;
}
#endif

using namespace std;
using namespace Util;

namespace Vulkan
{
Device::Device()
    : framebuffer_allocator(this)
    , transient_allocator(this)
#ifdef GRANITE_VULKAN_FILESYSTEM
	, shader_manager(this)
	, texture_manager(this)
#endif
{
#ifdef GRANITE_VULKAN_MT
	cookie.store(0);
#endif

	if (const char *env = getenv("GRANITE_TIMESTAMP_TRACE"))
	{
		LOGI("Tracing timestamps to %s.\n", env);
		if (!init_timestamp_trace(env))
			LOGE("Failed to init timestamp trace.\n");
	}
}

Semaphore Device::request_legacy_semaphore()
{
	LOCK();
	auto semaphore = managers.semaphore.request_cleared_semaphore();
	Semaphore ptr(handle_pool.semaphores.allocate(this, semaphore, false));
	return ptr;
}

Semaphore Device::request_external_semaphore(VkSemaphore semaphore, bool signalled)
{
	LOCK();
	VK_ASSERT(semaphore);
	Semaphore ptr(handle_pool.semaphores.allocate(this, semaphore, signalled));
	return ptr;
}

#ifndef _WIN32
Semaphore Device::request_imported_semaphore(int fd, VkExternalSemaphoreHandleTypeFlagBitsKHR handle_type)
{
	LOCK();
	if (!ext.supports_external)
		return {};

	VkExternalSemaphorePropertiesKHR props = { VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES_KHR };
	VkPhysicalDeviceExternalSemaphoreInfoKHR info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO_KHR };
	info.handleType = handle_type;

	vkGetPhysicalDeviceExternalSemaphorePropertiesKHR(gpu, &info, &props);
	if ((props.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT_KHR) == 0)
		return Semaphore(nullptr);

	auto semaphore = managers.semaphore.request_cleared_semaphore();

	VkImportSemaphoreFdInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR };
	import.fd = fd;
	import.semaphore = semaphore;
	import.handleType = handle_type;
	import.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR;
	Semaphore ptr(handle_pool.semaphores.allocate(this, semaphore, false));

	if (table->vkImportSemaphoreFdKHR(device, &import) != VK_SUCCESS)
		return Semaphore(nullptr);

	ptr->signal_external();
	ptr->destroy_on_consume();
	return ptr;
}
#endif

void Device::add_wait_semaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush)
{
	LOCK();
	add_wait_semaphore_nolock(type, semaphore, stages, flush);
}

void Device::add_wait_semaphore_nolock(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages,
                                       bool flush)
{
	VK_ASSERT(stages != 0);
	if (flush)
		flush_frame(type);
	auto &data = get_queue_data(type);

#ifdef VULKAN_DEBUG
	for (auto &sem : data.wait_semaphores)
		VK_ASSERT(sem.get() != semaphore.get());
#endif

	semaphore->signal_pending_wait();
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

	return LinearHostImageHandle(handle_pool.linear_images.allocate(this, move(gpu_image), move(cpu_image), info.stages));
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
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		cmd->copy_buffer_to_image(image.get_image(), image.get_host_visible_buffer(),
		                          0, {},
		                          { image.get_image().get_width(), image.get_image().get_height(), 1 },
		                          0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

		// Don't care about dstAccessMask, semaphore takes care of everything.
		cmd->image_barrier(image.get_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);

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

Shader *Device::request_shader(const uint32_t *data, size_t size)
{
	Util::Hasher hasher;
	hasher.data(data, size);

	auto hash = hasher.get();
	auto *ret = shaders.find(hash);
	if (!ret)
		ret = shaders.emplace_yield(hash, hash, this, data, size);
	return ret;
}

Shader *Device::request_shader_by_hash(Hash hash)
{
	return shaders.find(hash);
}

Program *Device::request_program(Vulkan::Shader *compute_shader)
{
	if (!compute_shader)
		return nullptr;

	Util::Hasher hasher;
	hasher.u64(compute_shader->get_hash());

	auto hash = hasher.get();
	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.emplace_yield(hash, this, compute_shader);
	return ret;
}

Program *Device::request_program(const uint32_t *compute_data, size_t compute_size)
{
	if (!compute_size)
		return nullptr;

	auto *compute_shader = request_shader(compute_data, compute_size);
	return request_program(compute_shader);
}

Program *Device::request_program(Shader *vertex, Shader *fragment)
{
	if (!vertex || !fragment)
		return nullptr;

	Util::Hasher hasher;
	hasher.u64(vertex->get_hash());
	hasher.u64(fragment->get_hash());

	auto hash = hasher.get();
	auto *ret = programs.find(hash);

	if (!ret)
		ret = programs.emplace_yield(hash, this, vertex, fragment);
	return ret;
}

Program *Device::request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
                                 size_t fragment_size)
{
	if (!vertex_size || !fragment_size)
		return nullptr;

	auto *vertex = request_shader(vertex_data, vertex_size);
	auto *fragment = request_shader(fragment_data, fragment_size);
	return request_program(vertex, fragment);
}

PipelineLayout *Device::request_pipeline_layout(const CombinedResourceLayout &layout)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(layout.sets), sizeof(layout.sets));
	h.data(&layout.stages_for_bindings[0][0], sizeof(layout.stages_for_bindings));
	h.u32(layout.push_constant_range.stageFlags);
	h.u32(layout.push_constant_range.size);
	h.data(layout.spec_constant_mask, sizeof(layout.spec_constant_mask));
	h.u32(layout.attribute_mask);
	h.u32(layout.render_target_mask);

	auto hash = h.get();
	auto *ret = pipeline_layouts.find(hash);
	if (!ret)
		ret = pipeline_layouts.emplace_yield(hash, hash, this, layout);
	return ret;
}

DescriptorSetAllocator *Device::request_descriptor_set_allocator(const DescriptorSetLayout &layout, const uint32_t *stages_for_bindings)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(&layout), sizeof(layout));
	h.data(stages_for_bindings, sizeof(uint32_t) * VULKAN_NUM_BINDINGS);
	auto hash = h.get();

	auto *ret = descriptor_set_allocators.find(hash);
	if (!ret)
		ret = descriptor_set_allocators.emplace_yield(hash, hash, this, layout, stages_for_bindings);
	return ret;
}

void Device::bake_program(Program &program)
{
	CombinedResourceLayout layout;
	if (program.get_shader(ShaderStage::Vertex))
		layout.attribute_mask = program.get_shader(ShaderStage::Vertex)->get_layout().input_mask;
	if (program.get_shader(ShaderStage::Fragment))
		layout.render_target_mask = program.get_shader(ShaderStage::Fragment)->get_layout().output_mask;

	layout.descriptor_set_mask = 0;

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
			layout.sets[set].sampled_buffer_mask |= shader_layout.sets[set].sampled_buffer_mask;
			layout.sets[set].input_attachment_mask |= shader_layout.sets[set].input_attachment_mask;
			layout.sets[set].sampler_mask |= shader_layout.sets[set].sampler_mask;
			layout.sets[set].separate_image_mask |= shader_layout.sets[set].separate_image_mask;
			layout.sets[set].fp_mask |= shader_layout.sets[set].fp_mask;

			for_each_bit(shader_layout.sets[set].immutable_sampler_mask, [&](uint32_t binding) {
				StockSampler sampler = get_immutable_sampler(shader_layout.sets[set], binding);

				// Do we already have an immutable sampler? Make sure it matches the layout.
				if (has_immutable_sampler(layout.sets[set], binding))
				{
					if (sampler != get_immutable_sampler(layout.sets[set], binding))
						LOGE("Immutable sampler mismatch detected!\n");
				}

				set_immutable_sampler(layout.sets[set], binding, sampler);
			});

			uint32_t active_binds =
					shader_layout.sets[set].sampled_image_mask |
					shader_layout.sets[set].storage_image_mask |
					shader_layout.sets[set].uniform_buffer_mask|
					shader_layout.sets[set].storage_buffer_mask |
					shader_layout.sets[set].sampled_buffer_mask |
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
		if (layout.stages_for_sets[set] != 0)
		{
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
	}

	Hasher h;
	h.u32(layout.push_constant_range.stageFlags);
	h.u32(layout.push_constant_range.size);
	layout.push_constant_layout_hash = h.get();
	program.set_pipeline_layout(request_pipeline_layout(layout));
}

bool Device::init_pipeline_cache(const uint8_t *data, size_t size)
{
	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);

	VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	if (!data || size < uuid_size)
	{
		LOGI("Creating a fresh pipeline cache.\n");
	}
	else if (memcmp(data, gpu_props.pipelineCacheUUID, uuid_size) != 0)
	{
		LOGI("Pipeline cache UUID changed.\n");
	}
	else
	{
		info.initialDataSize = size - uuid_size;
		info.pInitialData = data + uuid_size;
		LOGI("Initializing pipeline cache.\n");
	}

	if (pipeline_cache != VK_NULL_HANDLE)
		table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
	pipeline_cache = VK_NULL_HANDLE;
	return table->vkCreatePipelineCache(device, &info, nullptr, &pipeline_cache) == VK_SUCCESS;
}

static inline char to_hex(uint8_t v)
{
	if (v < 10)
		return char('0' + v);
	else
		return char('a' + (v - 10));
}

string Device::get_pipeline_cache_string() const
{
	string res;
	res.reserve(sizeof(gpu_props.pipelineCacheUUID) * 2);

	for (auto &c : gpu_props.pipelineCacheUUID)
	{
		res += to_hex(uint8_t((c >> 4) & 0xf));
		res += to_hex(uint8_t(c & 0xf));
	}

	return res;
}

void Device::init_pipeline_cache()
{
#ifdef GRANITE_VULKAN_FILESYSTEM
	auto file = Granite::Global::filesystem()->open(Util::join("cache://pipeline_cache_", get_pipeline_cache_string(), ".bin"),
	                                                Granite::FileMode::ReadOnly);
	if (file)
	{
		auto size = file->get_size();
		auto *mapped = static_cast<uint8_t *>(file->map());
		if (mapped && !init_pipeline_cache(mapped, size))
			LOGE("Failed to initialize pipeline cache.\n");
	}
	else if (!init_pipeline_cache(nullptr, 0))
		LOGE("Failed to initialize pipeline cache.\n");
#endif
}

size_t Device::get_pipeline_cache_size()
{
	if (pipeline_cache == VK_NULL_HANDLE)
		return 0;

	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	size_t size = 0;
	if (table->vkGetPipelineCacheData(device, pipeline_cache, &size, nullptr) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return 0;
	}

	return size + uuid_size;
}

bool Device::get_pipeline_cache_data(uint8_t *data, size_t size)
{
	if (pipeline_cache == VK_NULL_HANDLE)
		return false;

	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	if (size < uuid_size)
		return false;

	size -= uuid_size;
	memcpy(data, gpu_props.pipelineCacheUUID, uuid_size);
	data += uuid_size;

	if (table->vkGetPipelineCacheData(device, pipeline_cache, &size, data) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return false;
	}

	return true;
}

void Device::flush_pipeline_cache()
{
#ifdef GRANITE_VULKAN_FILESYSTEM
	size_t size = get_pipeline_cache_size();
	if (!size)
	{
		LOGE("Failed to get pipeline cache size.\n");
		return;
	}

	auto file = Granite::Global::filesystem()->open(Util::join("cache://pipeline_cache_", get_pipeline_cache_string(), ".bin"),
	                                                Granite::FileMode::WriteOnly);
	if (!file)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}

	uint8_t *data = static_cast<uint8_t *>(file->map_write(size));
	if (!data)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}

	if (!get_pipeline_cache_data(data, size))
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}
#endif
}

void Device::init_workarounds()
{
	workarounds = {};

#ifdef __APPLE__
	// Events are not supported in MoltenVK.
	workarounds.emulate_event_as_pipeline_barrier = true;
	LOGW("Emulating events as pipeline barriers on Metal emulation.\n");
#else
	if (gpu_props.vendorID == VENDOR_ID_NVIDIA &&
#ifdef _WIN32
	    VK_VERSION_MAJOR(gpu_props.driverVersion) < 417)
#else
	    VK_VERSION_MAJOR(gpu_props.driverVersion) < 415)
#endif
	{
		workarounds.force_store_in_render_pass = true;
		LOGW("Detected workaround for render pass STORE_OP_STORE.\n");
	}

	if (gpu_props.vendorID == VENDOR_ID_QCOM)
	{
		// Apparently, we need to use STORE_OP_STORE in all render passes no matter what ...
		workarounds.force_store_in_render_pass = true;
		workarounds.broken_color_write_mask = true;
		LOGW("Detected workaround for render pass STORE_OP_STORE.\n");
		LOGW("Detected workaround for broken color write masks.\n");
	}

	// UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL stalls, so need to acquire async.
	if (gpu_props.vendorID == VENDOR_ID_ARM)
	{
		LOGW("Workaround applied: Acquiring WSI images early on Mali.\n");
		LOGW("Workaround applied: Emulating events as pipeline barriers.\n");
		LOGW("Workaround applied: Optimize ALL_GRAPHICS_BIT barriers.\n");

		// All performance related workarounds.
		workarounds.wsi_acquire_barrier_is_expensive = true;
		workarounds.emulate_event_as_pipeline_barrier = true;
		workarounds.optimize_all_graphics_barrier = true;
	}
#endif
}

void Device::set_context(const Context &context)
{
	table = &context.get_device_table();

#ifdef GRANITE_VULKAN_MT
	register_thread_index(0);
#endif
	instance = context.get_instance();
	gpu = context.get_gpu();
	device = context.get_device();
	num_thread_indices = context.get_num_thread_indices();

	graphics_queue_family_index = context.get_graphics_queue_family();
	graphics_queue = context.get_graphics_queue();
	compute_queue_family_index = context.get_compute_queue_family();
	compute_queue = context.get_compute_queue();
	transfer_queue_family_index = context.get_transfer_queue_family();
	transfer_queue = context.get_transfer_queue();
	timestamp_valid_bits = context.get_timestamp_valid_bits();

	mem_props = context.get_mem_props();
	gpu_props = context.get_gpu_props();
	ext = context.get_enabled_device_features();

	init_workarounds();

	init_stock_samplers();
	init_pipeline_cache();

	init_timeline_semaphores();
	init_bindless();

#ifdef ANDROID
	init_frame_contexts(3); // Android needs a bit more ... ;)
#else
	init_frame_contexts(2); // By default, regular double buffer between CPU and GPU.
#endif

	managers.memory.init(this);
	managers.memory.set_supports_dedicated_allocation(ext.supports_dedicated);
	managers.semaphore.init(this);
	managers.fence.init(this);
	managers.event.init(this);
	managers.vbo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                  ImplementationQuirks::get().staging_need_device_local);
	managers.ibo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	                  ImplementationQuirks::get().staging_need_device_local);
	managers.ubo.init(this, 256 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment),
	                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                  ImplementationQuirks::get().staging_need_device_local);
	managers.ubo.set_spill_region_size(VULKAN_MAX_UBO_SIZE);
	managers.staging.init(this, 64 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.optimalBufferCopyOffsetAlignment),
	                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                      false);

	graphics.performance_query_pool.init_device(this, graphics_queue_family_index);
	if (graphics_queue_family_index != compute_queue_family_index)
		compute.performance_query_pool.init_device(this, compute_queue_family_index);
	if (graphics_queue_family_index != transfer_queue_family_index &&
	    compute_queue_family_index != transfer_queue_family_index)
	{
		transfer.performance_query_pool.init_device(this, transfer_queue_family_index);
	}

#ifdef GRANITE_VULKAN_FOSSILIZE
	init_pipeline_state();
#endif
#ifdef GRANITE_VULKAN_FILESYSTEM
	init_shader_manager_cache();
#endif

	init_calibrated_timestamps();
}

void Device::init_bindless()
{
	if (!ext.supports_descriptor_indexing)
		return;

	DescriptorSetLayout layout;

	layout.array_size[0] = DescriptorSetLayout::UNSIZED_ARRAY;
	for (unsigned i = 1; i < VULKAN_NUM_BINDINGS; i++)
		layout.array_size[i] = 1;

	layout.separate_image_mask = 1;
	uint32_t stages_for_sets[VULKAN_NUM_BINDINGS] = { VK_SHADER_STAGE_ALL };
	bindless_sampled_image_allocator_integer = request_descriptor_set_allocator(layout, stages_for_sets);
	layout.fp_mask = 1;
	bindless_sampled_image_allocator_fp = request_descriptor_set_allocator(layout, stages_for_sets);
}

void Device::init_timeline_semaphores()
{
	if (!ext.timeline_semaphore_features.timelineSemaphore)
		return;

	VkSemaphoreTypeCreateInfoKHR type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR };
	VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	info.pNext = &type_info;
	type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
	type_info.initialValue = 0;
	if (table->vkCreateSemaphore(device, &info, nullptr, &graphics.timeline_semaphore) != VK_SUCCESS)
		LOGE("Failed to create timeline semaphore.\n");
	if (table->vkCreateSemaphore(device, &info, nullptr, &compute.timeline_semaphore) != VK_SUCCESS)
		LOGE("Failed to create timeline semaphore.\n");
	if (table->vkCreateSemaphore(device, &info, nullptr, &transfer.timeline_semaphore) != VK_SUCCESS)
		LOGE("Failed to create timeline sempahore.\n");
}

void Device::init_stock_samplers()
{
	if (ext.sampler_ycbcr_conversion_features.samplerYcbcrConversion)
	{
		for (auto &sampler : samplers_ycbcr)
		{
			if (sampler)
				table->vkDestroySamplerYcbcrConversion(device, sampler, nullptr);
			sampler = VK_NULL_HANDLE;
		}

		VkSamplerYcbcrConversionCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO };
		info.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
		info.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
		info.components = {
			VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY,
		};
		info.chromaFilter = VK_FILTER_LINEAR;
		info.xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
		info.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
		info.forceExplicitReconstruction = VK_FALSE;

		info.format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
		table->vkCreateSamplerYcbcrConversionKHR(device, &info, nullptr,
		                                         &samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV420P_3PLANE)]);

		info.format = VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM;
		table->vkCreateSamplerYcbcrConversionKHR(device, &info, nullptr,
		                                         &samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV422P_3PLANE)]);

		info.format = VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM;
		table->vkCreateSamplerYcbcrConversionKHR(device, &info, nullptr,
		                                         &samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV444P_3PLANE)]);
	}

	SamplerCreateInfo info = {};
	info.max_lod = VK_LOD_CLAMP_NONE;
	info.max_anisotropy = 1.0f;

	for (unsigned i = 0; i < static_cast<unsigned>(StockSampler::Count); i++)
	{
		auto mode = static_cast<StockSampler>(i);

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
			info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			break;

		default:
			info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		}

		switch (mode)
		{
		case StockSampler::LinearClamp:
		case StockSampler::LinearWrap:
		case StockSampler::TrilinearClamp:
		case StockSampler::TrilinearWrap:
		case StockSampler::LinearShadow:
		case StockSampler::LinearYUV420P:
		case StockSampler::LinearYUV422P:
		case StockSampler::LinearYUV444P:
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
		case StockSampler::LinearWrap:
		case StockSampler::NearestWrap:
		case StockSampler::TrilinearWrap:
			info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;

		case StockSampler::LinearClamp:
		case StockSampler::NearestClamp:
		case StockSampler::TrilinearClamp:
		case StockSampler::NearestShadow:
		case StockSampler::LinearShadow:
		case StockSampler::LinearYUV420P:
		case StockSampler::LinearYUV422P:
		case StockSampler::LinearYUV444P:
			info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		}

		samplers[i] = create_sampler(info, mode);
	}
}

static void request_block(Device &device, BufferBlock &block, VkDeviceSize size,
                          BufferPool &pool, std::vector<BufferBlock> *dma, std::vector<BufferBlock> &recycle)
{
	if (block.mapped)
		device.unmap_host_buffer(*block.cpu, MEMORY_ACCESS_WRITE_BIT);

	if (block.offset == 0)
	{
		if (block.size == pool.get_block_size())
			pool.recycle_block(move(block));
	}
	else
	{
		if (block.cpu != block.gpu)
		{
			VK_ASSERT(dma);
			dma->push_back(block);
		}

		if (block.size == pool.get_block_size())
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
	request_block(*this, block, size, managers.vbo, &dma.vbo, frame().vbo_blocks);
}

void Device::request_index_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_index_block_nolock(block, size);
}

void Device::request_index_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.ibo, &dma.ibo, frame().ibo_blocks);
}

void Device::request_uniform_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_uniform_block_nolock(block, size);
}

void Device::request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.ubo, &dma.ubo, frame().ubo_blocks);
}

void Device::request_staging_block(BufferBlock &block, VkDeviceSize size)
{
	LOCK();
	request_staging_block_nolock(block, size);
}

void Device::request_staging_block_nolock(BufferBlock &block, VkDeviceSize size)
{
	request_block(*this, block, size, managers.staging, nullptr, frame().staging_blocks);
}

void Device::submit(CommandBufferHandle &cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	cmd->end_debug_channel();

	LOCK();
	submit_nolock(move(cmd), fence, semaphore_count, semaphores);
}

CommandBuffer::Type Device::get_physical_queue_type(CommandBuffer::Type queue_type) const
{
	if (queue_type != CommandBuffer::Type::AsyncGraphics)
	{
		return queue_type;
	}
	else
	{
		if (graphics_queue_family_index == compute_queue_family_index && graphics_queue != compute_queue)
			return CommandBuffer::Type::AsyncCompute;
		else
			return CommandBuffer::Type::Generic;
	}
}

void Device::submit_nolock(CommandBufferHandle cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
{
	auto type = cmd->get_command_buffer_type();
	auto &submissions = get_queue_submissions(type);
#ifdef VULKAN_DEBUG
	auto &pool = get_command_pool(type, cmd->get_thread_index());
	pool.signal_submitted(cmd->get_command_buffer());
#endif

	bool profiled_submit = cmd->has_profiling();

	if (profiled_submit)
	{
		LOGI("Submitting profiled command buffer, draining GPU.\n");
		auto &query_pool = get_performance_query_pool(type);
		// Profiled submit, drain GPU before submitting to make sure there's no overlap going on.
		query_pool.end_command_buffer(cmd->get_command_buffer());
		Fence drain_fence;
		submit_empty_nolock(type, &drain_fence, 0, nullptr, -1);
		drain_fence->wait();
		drain_fence->set_internal_sync_object();
	}

	cmd->end();
	submissions.push_back(move(cmd));

	InternalFence signalled_fence;

	if (fence || semaphore_count)
	{
		submit_queue(type, fence ? &signalled_fence : nullptr,
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
		auto &query_pool = get_performance_query_pool(type);
		Fence drain_fence;
		submit_empty_nolock(type, &drain_fence, 0, nullptr, fence || semaphore_count ? -1 : 0);
		drain_fence->wait();
		drain_fence->set_internal_sync_object();
		query_pool.report();
	}

	decrement_frame_counter_nolock();
}

void Device::submit_empty(CommandBuffer::Type type, Fence *fence,
                          unsigned semaphore_count, Semaphore *semaphores)
{
	LOCK();
	submit_empty_nolock(type, fence, semaphore_count, semaphores, -1);
}

void Device::submit_empty_nolock(CommandBuffer::Type type, Fence *fence,
                                 unsigned semaphore_count, Semaphore *semaphores, int profiling_iteration)
{
	if (type != CommandBuffer::Type::AsyncTransfer)
		flush_frame(CommandBuffer::Type::AsyncTransfer);

	InternalFence signalled_fence;
	submit_queue(type, fence ? &signalled_fence : nullptr, semaphore_count, semaphores, profiling_iteration);
	if (fence)
	{
		if (signalled_fence.value)
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
		else
			*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
	}
}

void Device::submit_empty_inner(CommandBuffer::Type type, InternalFence *fence,
                                unsigned semaphore_count, Semaphore *semaphores)
{
	auto &data = get_queue_data(type);
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	VkTimelineSemaphoreSubmitInfoKHR timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

	if (ext.timeline_semaphore_features.timelineSemaphore)
		submit.pNext = &timeline_info;

	VkSemaphore timeline_semaphore = data.timeline_semaphore;
	uint64_t timeline_value = ++data.current_timeline;

	VkQueue queue = get_vk_queue(type);
	switch (type)
	{
	default:
	case CommandBuffer::Type::Generic:
		frame().timeline_fence_graphics = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (ext.timeline_semaphore_features.timelineSemaphore)
		{
			LOGI("Signal graphics: (%p) %u\n",
			     reinterpret_cast<void *>(timeline_semaphore),
			     unsigned(data.current_timeline));
		}
#endif
		break;

	case CommandBuffer::Type::AsyncCompute:
		frame().timeline_fence_compute = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (ext.timeline_semaphore_features.timelineSemaphore)
		{
			LOGI("Signal compute: (%p) %u\n",
			     reinterpret_cast<void *>(timeline_semaphore),
			     unsigned(data.current_timeline));
		}
#endif
		break;

	case CommandBuffer::Type::AsyncTransfer:
		frame().timeline_fence_transfer = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (ext.timeline_semaphore_features.timelineSemaphore)
		{
			LOGI("Signal transfer: (%p) %u\n",
			     reinterpret_cast<void *>(timeline_semaphore),
			     unsigned(data.current_timeline));
		}
#endif
		break;
	}

	// Add external signal semaphores.
	SmallVector<VkSemaphore> signals;
	if (ext.timeline_semaphore_features.timelineSemaphore)
	{
		// Signal once and distribute the timeline value to all.
		timeline_info.signalSemaphoreValueCount = 1;
		timeline_info.pSignalSemaphoreValues = &timeline_value;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &timeline_semaphore;

		if (fence)
		{
			fence->timeline = timeline_semaphore;
			fence->value = timeline_value;
			fence->fence = VK_NULL_HANDLE;
		}

		for (unsigned i = 0; i < semaphore_count; i++)
		{
			VK_ASSERT(!semaphores[i]);
			semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, timeline_value, timeline_semaphore));
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
			signals.push_back(cleared_semaphore);
			VK_ASSERT(!semaphores[i]);
			semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true));
		}

		submit.signalSemaphoreCount = signals.size();
		if (!signals.empty())
			submit.pSignalSemaphores = signals.data();
	}

	// Add external wait semaphores.
	SmallVector<VkSemaphore> waits;
	SmallVector<uint64_t> waits_count;
	auto stages = move(data.wait_stages);

	for (auto &semaphore : data.wait_semaphores)
	{
		auto wait = semaphore->consume();
		if (!semaphore->get_timeline_value())
		{
			if (semaphore->can_recycle())
				frame().recycled_semaphores.push_back(wait);
			else
				frame().destroyed_semaphores.push_back(wait);
		}
		waits.push_back(wait);
		waits_count.push_back(semaphore->get_timeline_value());
	}

	data.wait_stages.clear();
	data.wait_semaphores.clear();

	submit.waitSemaphoreCount = waits.size();
	if (!stages.empty())
		submit.pWaitDstStageMask = stages.data();
	if (!waits.empty())
		submit.pWaitSemaphores = waits.data();

	if (!waits_count.empty())
	{
		timeline_info.waitSemaphoreValueCount = waits_count.size();
		timeline_info.pWaitSemaphoreValues = waits_count.data();
	}

	VkFence cleared_fence = fence && !ext.timeline_semaphore_features.timelineSemaphore ?
	                        managers.fence.request_cleared_fence() :
	                        VK_NULL_HANDLE;
	if (fence)
		fence->fence = cleared_fence;

	QueryPoolHandle start_ts, end_ts;
	if (json_timestamp_origin)
		start_ts = write_calibrated_timestamp_nolock();

	if (queue_lock_callback)
		queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
	if (cleared_fence)
		LOGI("Signalling Fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif

	VkResult result = table->vkQueueSubmit(queue, 1, &submit, cleared_fence);
	if (ImplementationQuirks::get().queue_wait_on_submission)
		table->vkQueueWaitIdle(queue);
	if (queue_unlock_callback)
		queue_unlock_callback();

	if (json_timestamp_origin)
	{
		end_ts = write_calibrated_timestamp_nolock();
		register_time_interval_nolock("CPU", std::move(start_ts), std::move(end_ts), "submit", "");
	}

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", int(result));
	if (result == VK_ERROR_DEVICE_LOST)
		report_checkpoints();

	if (!ext.timeline_semaphore_features.timelineSemaphore)
		data.need_fence = true;

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
	const char *queue_name = nullptr;
	switch (type)
	{
	default:
	case CommandBuffer::Type::Generic:
		queue_name = "Graphics";
		break;
	case CommandBuffer::Type::AsyncCompute:
		queue_name = "Compute";
		break;
	case CommandBuffer::Type::AsyncTransfer:
		queue_name = "Transfer";
		break;
	}

	LOGI("Empty submission to %s queue:\n", queue_name);
	for (uint32_t i = 0; i < submit.waitSemaphoreCount; i++)
	{
		LOGI("  Waiting for semaphore: %llx in stages %s\n",
		     reinterpret_cast<unsigned long long>(submit.pWaitSemaphores[i]),
		     stage_flags_to_string(submit.pWaitDstStageMask[i]).c_str());
	}

	for (uint32_t i = 0; i < submit.signalSemaphoreCount; i++)
	{
		LOGI("  Signalling semaphore: %llx\n",
		     reinterpret_cast<unsigned long long>(submit.pSignalSemaphores[i]));
	}
#endif
}

Fence Device::request_legacy_fence()
{
	VkFence fence = managers.fence.request_cleared_fence();
	return Fence(handle_pool.fences.allocate(this, fence));
}

void Device::submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush)
{
	auto access = buffer_usage_to_possible_access(usage);
	auto stages = buffer_usage_to_possible_stages(usage);
	VkQueue src_queue = get_vk_queue(cmd->get_command_buffer_type());

	if (src_queue == graphics_queue && src_queue == compute_queue)
	{
		// For single-queue systems, just use a pipeline barrier.
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, stages, access);
		submit_nolock(cmd, nullptr, 0, nullptr);
	}
	else
	{
		auto compute_stages = stages &
		                      (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
		                       VK_PIPELINE_STAGE_TRANSFER_BIT |
		                       VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

		auto compute_access = access &
		                      (VK_ACCESS_SHADER_READ_BIT |
		                       VK_ACCESS_SHADER_WRITE_BIT |
		                       VK_ACCESS_TRANSFER_READ_BIT |
		                       VK_ACCESS_UNIFORM_READ_BIT |
		                       VK_ACCESS_TRANSFER_WRITE_BIT |
		                       VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		auto graphics_stages = stages;

		if (src_queue == graphics_queue)
		{
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             graphics_stages, access);

			if (compute_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
		else if (src_queue == compute_queue)
		{
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             compute_stages, compute_access);

			if (graphics_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
		else
		{
			if (graphics_stages != 0 && compute_stages != 0)
			{
				Semaphore semaphores[2];
				submit_nolock(cmd, nullptr, 2, semaphores);
				add_wait_semaphore_nolock(CommandBuffer::Type::Generic, semaphores[0], graphics_stages, flush);
				add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, semaphores[1], compute_stages, flush);
			}
			else if (graphics_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
			}
			else if (compute_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, 1, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
			}
			else
				submit_nolock(cmd, nullptr, 0, nullptr);
		}
	}
}

void Device::submit_queue(CommandBuffer::Type type, InternalFence *fence,
                          unsigned semaphore_count, Semaphore *semaphores, int profiling_iteration)
{
	type = get_physical_queue_type(type);

	// Always check if we need to flush pending transfers.
	if (type != CommandBuffer::Type::AsyncTransfer)
		flush_frame(CommandBuffer::Type::AsyncTransfer);

	auto &data = get_queue_data(type);
	auto &submissions = get_queue_submissions(type);

	if (submissions.empty())
	{
		if (fence || semaphore_count)
			submit_empty_inner(type, fence, semaphore_count, semaphores);
		return;
	}

	VkSemaphore timeline_semaphore = data.timeline_semaphore;
	uint64_t timeline_value = ++data.current_timeline;

	VkQueue queue = get_vk_queue(type);
	switch (type)
	{
	default:
	case CommandBuffer::Type::Generic:
		frame().timeline_fence_graphics = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		LOGI("Signal graphics: (%p) %u\n",
			 reinterpret_cast<void *>(timeline_semaphore),
			 unsigned(data.current_timeline));
#endif
		break;

	case CommandBuffer::Type::AsyncCompute:
		frame().timeline_fence_compute = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		LOGI("Signal compute: (%p) %u\n",
			 reinterpret_cast<void *>(timeline_semaphore),
			 unsigned(data.current_timeline));
#endif
		break;

	case CommandBuffer::Type::AsyncTransfer:
		frame().timeline_fence_transfer = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		LOGI("Signal transfer: (%p) %u\n",
			 reinterpret_cast<void *>(timeline_semaphore),
			 unsigned(data.current_timeline));
#endif
		break;
	}

	SmallVector<VkCommandBuffer> cmds;
	cmds.reserve(submissions.size());

	SmallVector<VkSubmitInfo> submits;
	SmallVector<VkTimelineSemaphoreSubmitInfoKHR> timeline_infos;

	submits.reserve(2);
	timeline_infos.reserve(2);

	size_t last_cmd = 0;

	SmallVector<VkSemaphore> waits[2];
	SmallVector<uint64_t> wait_counts[2];
	SmallVector<VkFlags> wait_stages[2];
	SmallVector<VkSemaphore> signals[2];
	SmallVector<uint64_t> signal_counts[2];

	// Add external wait semaphores.
	wait_stages[0] = move(data.wait_stages);

	for (auto &semaphore : data.wait_semaphores)
	{
		auto wait = semaphore->consume();
		if (!semaphore->get_timeline_value())
		{
			if (semaphore->can_recycle())
				frame().recycled_semaphores.push_back(wait);
			else
				frame().destroyed_semaphores.push_back(wait);
		}
		wait_counts[0].push_back(semaphore->get_timeline_value());
		waits[0].push_back(wait);
	}
	data.wait_stages.clear();
	data.wait_semaphores.clear();

	for (auto &cmd : submissions)
	{
		if (cmd->swapchain_touched() && !wsi.touched && !wsi.consumed)
		{
			if (!cmds.empty())
			{
				// Push all pending cmd buffers to their own submission.
				submits.emplace_back();

				timeline_infos.emplace_back();
				auto &timeline_info = timeline_infos.back();
				timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

				auto &submit = submits.back();
				submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
				if (ext.timeline_semaphore_features.timelineSemaphore)
					submit.pNext = &timeline_info;

				submit.commandBufferCount = cmds.size() - last_cmd;
				submit.pCommandBuffers = cmds.data() + last_cmd;
				last_cmd = cmds.size();
			}
			wsi.touched = true;
		}

		cmds.push_back(cmd->get_command_buffer());
	}

	if (cmds.size() > last_cmd)
	{
		unsigned index = submits.size();

		// Push all pending cmd buffers to their own submission.
		timeline_infos.emplace_back();
		auto &timeline_info = timeline_infos.back();
		timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

		submits.emplace_back();
		auto &submit = submits.back();
		submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

		if (ext.timeline_semaphore_features.timelineSemaphore)
			submit.pNext = &timeline_info;

		submit.commandBufferCount = cmds.size() - last_cmd;
		submit.pCommandBuffers = cmds.data() + last_cmd;
		if (wsi.touched && !wsi.consumed)
		{
			static const VkFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			if (wsi.acquire && wsi.acquire->get_semaphore() != VK_NULL_HANDLE)
			{
				VK_ASSERT(wsi.acquire->is_signalled());
				VkSemaphore sem = wsi.acquire->consume();

				waits[index].push_back(sem);
				wait_counts[index].push_back(wsi.acquire->get_timeline_value());
				wait_stages[index].push_back(wait);

				if (!wsi.acquire->get_timeline_value())
				{
					if (wsi.acquire->can_recycle())
						frame().recycled_semaphores.push_back(sem);
					else
						frame().destroyed_semaphores.push_back(sem);
				}

				wsi.acquire.reset();
			}

			VkSemaphore release = managers.semaphore.request_cleared_semaphore();
			wsi.release = Semaphore(handle_pool.semaphores.allocate(this, release, true));
			wsi.release->set_internal_sync_object();
			signals[index].push_back(wsi.release->get_semaphore());
			signal_counts[index].push_back(0);
			wsi.consumed = true;
		}
		last_cmd = cmds.size();
	}

	VkFence cleared_fence = fence && !ext.timeline_semaphore_features.timelineSemaphore ?
	                        managers.fence.request_cleared_fence() :
	                        VK_NULL_HANDLE;

	if (fence)
		fence->fence = cleared_fence;

	// Add external signal semaphores.
	if (ext.timeline_semaphore_features.timelineSemaphore)
	{
		// Signal once and distribute the timeline value to all.
		signals[submits.size() - 1].push_back(timeline_semaphore);
		signal_counts[submits.size() - 1].push_back(timeline_value);

		if (fence)
		{
			fence->timeline = timeline_semaphore;
			fence->value = timeline_value;
			fence->fence = VK_NULL_HANDLE;
		}

		for (unsigned i = 0; i < semaphore_count; i++)
		{
			VK_ASSERT(!semaphores[i]);
			semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, timeline_value, timeline_semaphore));
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
			signals[submits.size() - 1].push_back(cleared_semaphore);
			signal_counts[submits.size() - 1].push_back(0);
			VK_ASSERT(!semaphores[i]);
			semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true));
		}
	}

	VkPerformanceQuerySubmitInfoKHR profiling_infos[2];

	for (unsigned i = 0; i < submits.size(); i++)
	{
		auto &submit = submits[i];
		auto &timeline_submit = timeline_infos[i];

		if (profiling_iteration >= 0)
		{
			profiling_infos[i] = { VK_STRUCTURE_TYPE_PERFORMANCE_QUERY_SUBMIT_INFO_KHR };
			profiling_infos[i].counterPassIndex = uint32_t(profiling_iteration);
			if (submit.pNext)
				timeline_submit.pNext = &profiling_infos[i];
			else
				submit.pNext = &profiling_infos[i];
		}

		submit.waitSemaphoreCount = waits[i].size();
		submit.pWaitSemaphores = waits[i].data();
		submit.pWaitDstStageMask = wait_stages[i].data();
		timeline_submit.waitSemaphoreValueCount = submit.waitSemaphoreCount;
		timeline_submit.pWaitSemaphoreValues = wait_counts[i].data();

		submit.signalSemaphoreCount = signals[i].size();
		submit.pSignalSemaphores = signals[i].data();
		timeline_submit.signalSemaphoreValueCount = submit.signalSemaphoreCount;
		timeline_submit.pSignalSemaphoreValues = signal_counts[i].data();
	}

	QueryPoolHandle start_ts, end_ts;
	if (json_timestamp_origin)
		start_ts = write_calibrated_timestamp_nolock();

	if (queue_lock_callback)
		queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
	if (cleared_fence)
		LOGI("Signalling fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif
	VkResult result = table->vkQueueSubmit(queue, submits.size(), submits.data(), cleared_fence);
	if (ImplementationQuirks::get().queue_wait_on_submission)
		table->vkQueueWaitIdle(queue);
	if (queue_unlock_callback)
		queue_unlock_callback();

	if (json_timestamp_origin)
	{
		end_ts = write_calibrated_timestamp_nolock();
		register_time_interval_nolock("CPU", std::move(start_ts), std::move(end_ts), "submit", "");
	}

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", int(result));
	if (result == VK_ERROR_DEVICE_LOST)
		report_checkpoints();
	submissions.clear();

	if (!ext.timeline_semaphore_features.timelineSemaphore)
		data.need_fence = true;

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
	const char *queue_name = nullptr;
	switch (type)
	{
	default:
	case CommandBuffer::Type::Generic:
		queue_name = "Graphics";
		break;
	case CommandBuffer::Type::AsyncCompute:
		queue_name = "Compute";
		break;
	case CommandBuffer::Type::AsyncTransfer:
		queue_name = "Transfer";
		break;
	}

	for (auto &submit : submits)
	{
		LOGI("Submission to %s queue:\n", queue_name);
		for (uint32_t i = 0; i < submit.waitSemaphoreCount; i++)
		{
			LOGI("  Waiting for semaphore: %llx in stages %s\n",
			     reinterpret_cast<unsigned long long>(submit.pWaitSemaphores[i]),
			     stage_flags_to_string(submit.pWaitDstStageMask[i]).c_str());
		}

		for (uint32_t i = 0; i < submit.commandBufferCount; i++)
			LOGI(" Command Buffer %llx\n", reinterpret_cast<unsigned long long>(submit.pCommandBuffers[i]));

		for (uint32_t i = 0; i < submit.signalSemaphoreCount; i++)
		{
			LOGI("  Signalling semaphore: %llx\n",
			     reinterpret_cast<unsigned long long>(submit.pSignalSemaphores[i]));
		}
	}
#endif
}

void Device::flush_frame(CommandBuffer::Type type)
{
	if (type == CommandBuffer::Type::AsyncTransfer)
		sync_buffer_blocks();
	submit_queue(type, nullptr, 0, nullptr);
}

void Device::sync_buffer_blocks()
{
	if (dma.vbo.empty() && dma.ibo.empty() && dma.ubo.empty())
		return;

	VkBufferUsageFlags usage = 0;

	auto cmd = request_command_buffer_nolock(get_thread_index(), CommandBuffer::Type::AsyncTransfer, false);

	cmd->begin_region("buffer-block-sync");

	for (auto &block : dma.vbo)
	{
		VK_ASSERT(block.offset != 0);
		cmd->copy_buffer(*block.gpu, 0, *block.cpu, 0, block.offset);
		usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}

	for (auto &block : dma.ibo)
	{
		VK_ASSERT(block.offset != 0);
		cmd->copy_buffer(*block.gpu, 0, *block.cpu, 0, block.offset);
		usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}

	for (auto &block : dma.ubo)
	{
		VK_ASSERT(block.offset != 0);
		cmd->copy_buffer(*block.gpu, 0, *block.cpu, 0, block.offset);
		usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}

	dma.vbo.clear();
	dma.ibo.clear();
	dma.ubo.clear();

	cmd->end_region();

	// Do not flush graphics or compute in this context.
	// We must be able to inject semaphores into all currently enqueued graphics / compute.
	submit_staging(cmd, usage, false);
}

void Device::end_frame_context()
{
	DRAIN_FRAME_LOCK();
	end_frame_nolock();
}

void Device::end_frame_nolock()
{
	// Kept handles alive until end-of-frame, free now if appropriate.
	for (auto &image : frame().keep_alive_images)
	{
		image->set_internal_sync_object();
		image->get_view().set_internal_sync_object();
	}
	frame().keep_alive_images.clear();

	// Make sure we have a fence which covers all submissions in the frame.
	InternalFence fence;

	if (transfer.need_fence || !frame().transfer_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::AsyncTransfer, &fence, 0, nullptr);
		if (fence.fence != VK_NULL_HANDLE)
		{
			frame().wait_fences.push_back(fence.fence);
			frame().recycle_fences.push_back(fence.fence);
		}
		transfer.need_fence = false;
	}

	if (graphics.need_fence || !frame().graphics_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::Generic, &fence, 0, nullptr);
		if (fence.fence != VK_NULL_HANDLE)
		{
			frame().wait_fences.push_back(fence.fence);
			frame().recycle_fences.push_back(fence.fence);
		}
		graphics.need_fence = false;
	}

	if (compute.need_fence || !frame().compute_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::AsyncCompute, &fence, 0, nullptr);
		if (fence.fence != VK_NULL_HANDLE)
		{
			frame().wait_fences.push_back(fence.fence);
			frame().recycle_fences.push_back(fence.fence);
		}
		compute.need_fence = false;
	}
}

void Device::flush_frame()
{
	LOCK();
	flush_frame_nolock();
}

void Device::flush_frame_nolock()
{
	flush_frame(CommandBuffer::Type::AsyncTransfer);
	flush_frame(CommandBuffer::Type::Generic);
	flush_frame(CommandBuffer::Type::AsyncCompute);
}

Device::QueueData &Device::get_queue_data(CommandBuffer::Type type)
{
	switch (get_physical_queue_type(type))
	{
	default:
	case CommandBuffer::Type::Generic:
		return graphics;
	case CommandBuffer::Type::AsyncCompute:
		return compute;
	case CommandBuffer::Type::AsyncTransfer:
		return transfer;
	}
}

VkQueue Device::get_vk_queue(CommandBuffer::Type type) const
{
	switch (get_physical_queue_type(type))
	{
	default:
	case CommandBuffer::Type::Generic:
		return graphics_queue;
	case CommandBuffer::Type::AsyncCompute:
		return compute_queue;
	case CommandBuffer::Type::AsyncTransfer:
		return transfer_queue;
	}
}

PerformanceQueryPool &Device::get_performance_query_pool(CommandBuffer::Type type)
{
	switch (get_physical_queue_type(type))
	{
	default:
	case CommandBuffer::Type::Generic:
		return graphics.performance_query_pool;
	case CommandBuffer::Type::AsyncCompute:
		if (graphics_queue_family_index == compute_queue_family_index)
			return graphics.performance_query_pool;
		else
			return compute.performance_query_pool;
	case CommandBuffer::Type::AsyncTransfer:
		if (graphics_queue_family_index == transfer_queue_family_index)
			return graphics.performance_query_pool;
		else if (compute_queue_family_index == transfer_queue_family_index)
			return compute.performance_query_pool;
		else
			return transfer.performance_query_pool;
	}
}

CommandPool &Device::get_command_pool(CommandBuffer::Type type, unsigned thread)
{
	switch (get_physical_queue_type(type))
	{
	default:
	case CommandBuffer::Type::Generic:
		return frame().graphics_cmd_pool[thread];
	case CommandBuffer::Type::AsyncCompute:
		return frame().compute_cmd_pool[thread];
	case CommandBuffer::Type::AsyncTransfer:
		return frame().transfer_cmd_pool[thread];
	}
}

Util::SmallVector<CommandBufferHandle> &Device::get_queue_submissions(CommandBuffer::Type type)
{
	switch (get_physical_queue_type(type))
	{
	default:
	case CommandBuffer::Type::Generic:
		return frame().graphics_submissions;
	case CommandBuffer::Type::AsyncCompute:
		return frame().compute_submissions;
	case CommandBuffer::Type::AsyncTransfer:
		return frame().transfer_submissions;
	}
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
#ifndef GRANITE_VULKAN_MT
	VK_ASSERT(thread_index == 0);
#endif
	auto cmd = get_command_pool(type, thread_index).request_command_buffer();

	if (profiled && !ext.performance_query_features.performanceCounterQueryPools)
	{
		LOGW("Profiling is not supported on this device.\n");
		profiled = false;
	}

	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	table->vkBeginCommandBuffer(cmd, &info);
	add_frame_counter_nolock();
	CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, pipeline_cache, type));
	handle->set_thread_index(thread_index);

	if (profiled)
	{
		auto &query_pool = get_performance_query_pool(type);
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
		auto &pool = get_command_pool(secondary.get_command_buffer_type(),
		                              secondary.get_thread_index());
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

	auto cmd = get_command_pool(type, thread_index).request_secondary_command_buffer();
	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	VkCommandBufferInheritanceInfo inherit = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };

	inherit.framebuffer = VK_NULL_HANDLE;
	inherit.renderPass = framebuffer->get_compatible_render_pass().get_render_pass();
	inherit.subpass = subpass;
	info.pInheritanceInfo = &inherit;
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

	table->vkBeginCommandBuffer(cmd, &info);
	add_frame_counter_nolock();
	CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, pipeline_cache, type));
	handle->set_thread_index(thread_index);
	handle->set_is_secondary();
	return handle;
}

void Device::set_acquire_semaphore(unsigned index, Semaphore acquire)
{
	wsi.acquire = move(acquire);
	wsi.index = index;
	wsi.touched = false;
	wsi.consumed = false;

	if (wsi.acquire)
	{
		wsi.acquire->set_internal_sync_object();
		VK_ASSERT(wsi.acquire->is_signalled());
	}
}

Semaphore Device::consume_release_semaphore()
{
	auto ret = move(wsi.release);
	wsi.release.reset();
	return ret;
}

const Sampler &Device::get_stock_sampler(StockSampler sampler) const
{
	return *samplers[static_cast<unsigned>(sampler)];
}

bool Device::swapchain_touched() const
{
	return wsi.touched;
}

Device::~Device()
{
	wait_idle();

	managers.timestamps.log_simple();

	wsi.acquire.reset();
	wsi.release.reset();
	wsi.swapchain.clear();

	if (pipeline_cache != VK_NULL_HANDLE)
	{
		flush_pipeline_cache();
		table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
	}

#ifdef GRANITE_VULKAN_FILESYSTEM
	flush_shader_manager_cache();
#endif

#ifdef GRANITE_VULKAN_FOSSILIZE
	flush_pipeline_state();
#endif

	framebuffer_allocator.clear();
	transient_allocator.clear();
	for (auto &sampler : samplers)
		sampler.reset();

	for (auto &sampler : samplers_ycbcr)
		if (sampler)
			table->vkDestroySamplerYcbcrConversion(device, sampler, nullptr);

	deinit_timeline_semaphores();
}

void Device::deinit_timeline_semaphores()
{
	if (graphics.timeline_semaphore != VK_NULL_HANDLE)
		table->vkDestroySemaphore(device, graphics.timeline_semaphore, nullptr);
	if (compute.timeline_semaphore != VK_NULL_HANDLE)
		table->vkDestroySemaphore(device, compute.timeline_semaphore, nullptr);
	if (transfer.timeline_semaphore != VK_NULL_HANDLE)
		table->vkDestroySemaphore(device, transfer.timeline_semaphore, nullptr);

	graphics.timeline_semaphore = VK_NULL_HANDLE;
	compute.timeline_semaphore = VK_NULL_HANDLE;
	transfer.timeline_semaphore = VK_NULL_HANDLE;

	// Make sure we don't accidentally try to wait for these after we destroy the semaphores.
	for (auto &frame : per_frame)
	{
		frame->timeline_fence_graphics = 0;
		frame->timeline_fence_compute = 0;
		frame->timeline_fence_transfer = 0;
		frame->graphics_timeline_semaphore = VK_NULL_HANDLE;
		frame->compute_timeline_semaphore = VK_NULL_HANDLE;
		frame->transfer_timeline_semaphore = VK_NULL_HANDLE;
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
		auto frame = unique_ptr<PerFrame>(new PerFrame(this, i));
		per_frame.emplace_back(move(frame));
	}
}

void Device::init_external_swapchain(const vector<ImageHandle> &swapchain_images)
{
	DRAIN_FRAME_LOCK();
	wsi.swapchain.clear();
	wait_idle_nolock();

	wsi.index = 0;
	wsi.touched = false;
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

void Device::init_swapchain(const vector<VkImage> &swapchain_images, unsigned width, unsigned height, VkFormat format)
{
	DRAIN_FRAME_LOCK();
	wsi.swapchain.clear();
	wait_idle_nolock();

	const auto info = ImageCreateInfo::render_target(width, height, format);

	wsi.index = 0;
	wsi.touched = false;
	wsi.consumed = false;
	for (auto &image : swapchain_images)
	{
		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.format = format;
		view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		view_info.components.g = VK_COMPONENT_SWIZZLE_G;
		view_info.components.b = VK_COMPONENT_SWIZZLE_B;
		view_info.components.a = VK_COMPONENT_SWIZZLE_A;
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
		wsi.swapchain.push_back(backbuffer);
		set_name(*backbuffer, "backbuffer");
		backbuffer->set_swapchain_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
}

Device::PerFrame::PerFrame(Device *device_, unsigned frame_index_)
    : device(*device_)
    , frame_index(frame_index_)
    , table(device_->get_device_table())
    , managers(device_->managers)
    , query_pool(device_)
{
	graphics_timeline_semaphore = device.graphics.timeline_semaphore;
	compute_timeline_semaphore = device.compute.timeline_semaphore;
	transfer_timeline_semaphore = device.transfer.timeline_semaphore;

	unsigned count = device_->num_thread_indices;
	graphics_cmd_pool.reserve(count);
	compute_cmd_pool.reserve(count);
	transfer_cmd_pool.reserve(count);
	for (unsigned i = 0; i < count; i++)
	{
		graphics_cmd_pool.emplace_back(device_, device_->graphics_queue_family_index);
		compute_cmd_pool.emplace_back(device_, device_->compute_queue_family_index);
		transfer_cmd_pool.emplace_back(device_, device_->transfer_queue_family_index);
	}
}

void Device::keep_handle_alive(ImageHandle handle)
{
	LOCK();
	frame().keep_alive_images.push_back(move(handle));
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

void Device::destroy_pipeline(VkPipeline pipeline)
{
	LOCK();
	destroy_pipeline_nolock(pipeline);
}

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

void Device::destroy_pipeline_nolock(VkPipeline pipeline)
{
	VK_ASSERT(!exists(frame().destroyed_pipelines, pipeline));
	frame().destroyed_pipelines.push_back(pipeline);
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
		frame().recycle_fences.push_back(fence);
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

void Device::clear_wait_semaphores()
{
	for (auto &sem : graphics.wait_semaphores)
		table->vkDestroySemaphore(device, sem->consume(), nullptr);
	for (auto &sem : compute.wait_semaphores)
		table->vkDestroySemaphore(device, sem->consume(), nullptr);
	for (auto &sem : transfer.wait_semaphores)
		table->vkDestroySemaphore(device, sem->consume(), nullptr);

	graphics.wait_semaphores.clear();
	graphics.wait_stages.clear();
	compute.wait_semaphores.clear();
	compute.wait_stages.clear();
	transfer.wait_semaphores.clear();
	transfer.wait_stages.clear();
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
		if (result == VK_ERROR_DEVICE_LOST)
			report_checkpoints();
		if (queue_unlock_callback)
			queue_unlock_callback();
	}

	clear_wait_semaphores();

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
	for (auto &allocator : descriptor_set_allocators)
		allocator.clear();

	for (auto &frame : per_frame)
	{
		// We have done WaitIdle, no need to wait for extra fences, it's also not safe.
		frame->wait_fences.clear();
		frame->begin();
	}
}

void Device::next_frame_context()
{
	DRAIN_FRAME_LOCK();

	if (frame_context_begin_ts)
	{
		auto frame_context_end_ts = write_calibrated_timestamp_nolock();
		register_time_interval_nolock("CPU", std::move(frame_context_begin_ts), std::move(frame_context_end_ts), "command submissions", "");
		frame_context_begin_ts = {};
	}

	// Flush the frame here as we might have pending staging command buffers from init stage.
	end_frame_nolock();

	framebuffer_allocator.begin_frame();
	transient_allocator.begin_frame();
	for (auto &allocator : descriptor_set_allocators)
		allocator.begin_frame();

	VK_ASSERT(!per_frame.empty());
	frame_context_index++;
	if (frame_context_index >= per_frame.size())
		frame_context_index = 0;

	frame().begin();
	recalibrate_timestamps();
	frame_context_begin_ts = write_calibrated_timestamp_nolock();
}

QueryPoolHandle Device::write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage)
{
	LOCK();
	return write_timestamp_nolock(cmd, stage);
}

QueryPoolHandle Device::write_timestamp_nolock(VkCommandBuffer cmd, VkPipelineStageFlagBits stage)
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
	if (!json_trace_file)
		return {};

	auto handle = QueryPoolHandle(handle_pool.query.allocate(this));
	handle->signal_timestamp_ticks(get_calibrated_timestamp());
	return handle;
}

void Device::recalibrate_timestamps_fallback()
{
	wait_idle_nolock();
	auto cmd = request_command_buffer_nolock(0, CommandBuffer::Type::Generic, false);
	auto ts = write_timestamp_nolock(cmd->get_command_buffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	if (!ts)
		return;
	auto start_ts = Util::get_current_time_nsecs();
	submit_nolock(cmd, nullptr, 0, nullptr);
	wait_idle_nolock();
	auto end_ts = Util::get_current_time_nsecs();
	auto host_ts = (start_ts + end_ts) / 2;

	LOGI("Calibrated timestamps with a fallback method. Uncertainty: %.3f us.\n", 1e-3 * (end_ts - start_ts));

	calibrated_timestamp_host = host_ts;
	VK_ASSERT(ts->is_signalled());
	calibrated_timestamp_device = ts->get_timestamp_ticks();
}

void Device::init_calibrated_timestamps()
{
	if (!get_device_features().supports_calibrated_timestamps)
	{
		recalibrate_timestamps_fallback();
		return;
	}

	uint32_t count;
	vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(gpu, &count, nullptr);
	std::vector<VkTimeDomainEXT> domains(count);
	if (vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(gpu, &count, domains.data()) != VK_SUCCESS)
		return;

	bool supports_device_domain = false;
	for (auto &domain : domains)
	{
		if (domain == VK_TIME_DOMAIN_DEVICE_EXT)
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
		const auto supported_domain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
#else
		const auto supported_domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT;
#endif
		if (domain == supported_domain)
		{
			calibrated_time_domain = domain;
			break;
		}
	}

	if (calibrated_time_domain == VK_TIME_DOMAIN_DEVICE_EXT)
	{
		LOGE("Could not find a suitable time domain for calibrated timestamps.\n");
		return;
	}

	if (!resample_calibrated_timestamps())
	{
		LOGE("Failed to get calibrated timestamps.\n");
		calibrated_time_domain = VK_TIME_DOMAIN_DEVICE_EXT;
		return;
	}
}

bool Device::resample_calibrated_timestamps()
{
	VkCalibratedTimestampInfoEXT infos[2] = {};
	infos[0].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
	infos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
	infos[0].timeDomain = calibrated_time_domain;
	infos[1].timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;
	uint64_t timestamps[2] = {};
	uint64_t max_deviation[2] = {};

	if (table->vkGetCalibratedTimestampsEXT(device, 2, infos, timestamps, max_deviation) != VK_SUCCESS)
	{
		LOGE("Failed to get calibrated timestamps.\n");
		calibrated_time_domain = VK_TIME_DOMAIN_DEVICE_EXT;
		return false;
	}

	calibrated_timestamp_host = timestamps[0];
	calibrated_timestamp_device = timestamps[1];

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
	if (!json_trace_file)
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

int64_t Device::get_calibrated_timestamp()
{
	int64_t nsecs = Util::get_current_time_nsecs();

	auto offset_from_calibration = double(nsecs - calibrated_timestamp_host);
	auto ticks_in_device_timebase = int64_t(offset_from_calibration / double(gpu_props.limits.timestampPeriod));
	int64_t reported = calibrated_timestamp_device + ticks_in_device_timebase;
	reported = std::max(reported, last_calibrated_timestamp_host);
	last_calibrated_timestamp_host = reported;
	return reported;
}

void Device::register_time_interval(std::string tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts, std::string tag, std::string extra)
{
	LOCK();
	register_time_interval_nolock(std::move(tid), std::move(start_ts), std::move(end_ts), std::move(tag), std::move(extra));
}

void Device::register_time_interval_nolock(std::string tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts,
                                           std::string tag, std::string extra)
{
	if (start_ts && end_ts)
	{
		TimestampInterval *timestamp_tag = managers.timestamps.get_timestamp_tag(tag.c_str());
#ifdef VULKAN_DEBUG
		if (start_ts->is_signalled() && end_ts->is_signalled())
			VK_ASSERT(end_ts->get_timestamp_ticks() >= start_ts->get_timestamp_ticks());
#endif
		frame().timestamp_intervals.push_back({ std::move(tid), move(start_ts), move(end_ts), timestamp_tag, std::move(extra) });
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
#ifdef GRANITE_VULKAN_MT
	lock.cond.notify_one();
#endif
}

void Device::PerFrame::begin()
{
	VkDevice vkdevice = device.get_device();

	Vulkan::QueryPoolHandle wait_fence_ts;
	if (!in_destructor && device.json_timestamp_origin)
		wait_fence_ts = device.write_calibrated_timestamp_nolock();

	if (device.get_device_features().timeline_semaphore_features.timelineSemaphore &&
	    graphics_timeline_semaphore && compute_timeline_semaphore && transfer_timeline_semaphore)
	{
		VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
		const VkSemaphore semaphores[3] = { graphics_timeline_semaphore, compute_timeline_semaphore, transfer_timeline_semaphore };
		const uint64_t values[3] = { timeline_fence_graphics, timeline_fence_compute, timeline_fence_transfer };

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (device.get_device_features().timeline_semaphore_features.timelineSemaphore)
		{
			LOGI("Waiting for graphics (%p) %u\n",
			     reinterpret_cast<void *>(graphics_timeline_semaphore),
			     unsigned(timeline_fence_graphics));
			LOGI("Waiting for compute (%p) %u\n",
			     reinterpret_cast<void *>(compute_timeline_semaphore),
			     unsigned(timeline_fence_compute));
			LOGI("Waiting for transfer (%p) %u\n",
			     reinterpret_cast<void *>(transfer_timeline_semaphore),
			     unsigned(timeline_fence_transfer));
		}
#endif

		info.pSemaphores = semaphores;
		info.pValues = values;
		info.semaphoreCount = 3;
		table.vkWaitSemaphoresKHR(vkdevice, &info, UINT64_MAX);
	}

	// If we're using timeline semaphores, these paths should never be hit.
	if (!wait_fences.empty())
	{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		for (auto &fence : wait_fences)
			LOGI("Waiting for Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
		table.vkWaitForFences(vkdevice, wait_fences.size(), wait_fences.data(), VK_TRUE, UINT64_MAX);
		wait_fences.clear();
	}

	if (!in_destructor && device.json_timestamp_origin)
		device.register_time_interval_nolock("CPU", std::move(wait_fence_ts), device.write_calibrated_timestamp_nolock(), "fence", "");

	// If we're using timeline semaphores, these paths should never be hit.
	if (!recycle_fences.empty())
	{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		for (auto &fence : recycle_fences)
			LOGI("Recycling Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
		table.vkResetFences(vkdevice, recycle_fences.size(), recycle_fences.data());
		for (auto &fence : recycle_fences)
			managers.fence.recycle_fence(fence);
		recycle_fences.clear();
	}

	for (auto &pool : graphics_cmd_pool)
		pool.begin();
	for (auto &pool : compute_cmd_pool)
		pool.begin();
	for (auto &pool : transfer_cmd_pool)
		pool.begin();
	query_pool.begin();

	for (auto &channel : debug_channels)
		device.parse_debug_channel(channel);

	// Free the debug channel buffers here, and they will immediately be recycled by the destroyed_buffers right below.
	debug_channels.clear();

	for (auto &framebuffer : destroyed_framebuffers)
		table.vkDestroyFramebuffer(vkdevice, framebuffer, nullptr);
	for (auto &sampler : destroyed_samplers)
		table.vkDestroySampler(vkdevice, sampler, nullptr);
	for (auto &pipeline : destroyed_pipelines)
		table.vkDestroyPipeline(vkdevice, pipeline, nullptr);
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
	for (auto &semaphore : recycled_semaphores)
	{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		LOGI("Recycling semaphore: %llx\n", reinterpret_cast<unsigned long long>(semaphore));
#endif
		managers.semaphore.recycle(semaphore);
	}
	for (auto &event : recycled_events)
		managers.event.recycle(event);
	for (auto &alloc : allocations)
		alloc.free_immediate(managers.memory);

	for (auto &block : vbo_blocks)
		managers.vbo.recycle_block(move(block));
	for (auto &block : ibo_blocks)
		managers.ibo.recycle_block(move(block));
	for (auto &block : ubo_blocks)
		managers.ubo.recycle_block(move(block));
	for (auto &block : staging_blocks)
		managers.staging.recycle_block(move(block));
	vbo_blocks.clear();
	ibo_blocks.clear();
	ubo_blocks.clear();
	staging_blocks.clear();

	destroyed_framebuffers.clear();
	destroyed_samplers.clear();
	destroyed_pipelines.clear();
	destroyed_image_views.clear();
	destroyed_buffer_views.clear();
	destroyed_images.clear();
	destroyed_buffers.clear();
	destroyed_semaphores.clear();
	destroyed_descriptor_pools.clear();
	recycled_semaphores.clear();
	recycled_events.clear();
	allocations.clear();

	int64_t min_timestamp_us = std::numeric_limits<int64_t>::max();
	int64_t max_timestamp_us = 0;

	for (auto &ts : timestamp_intervals)
	{
		if (ts.end_ts->is_signalled() && ts.start_ts->is_signalled())
		{
			ts.timestamp_tag->accumulate_time(
			    device.convert_timestamp_delta(ts.start_ts->get_timestamp_ticks(), ts.end_ts->get_timestamp_ticks()));
			device.write_json_timestamp_range(frame_index, ts.tid.c_str(), ts.timestamp_tag->get_tag().c_str(),
			                                  ts.extra.c_str(),
			                                  ts.start_ts->get_timestamp_ticks(), ts.end_ts->get_timestamp_ticks(),
			                                  min_timestamp_us, max_timestamp_us);
		}
	}
	device.write_json_timestamp_range_us(frame_index, "CPU + GPU", "full frame lifetime", min_timestamp_us, max_timestamp_us);
	managers.timestamps.mark_end_of_frame_context();
	timestamp_intervals.clear();
}

Device::PerFrame::~PerFrame()
{
	in_destructor = true;
	begin();
}

uint32_t Device::find_memory_type(BufferDomain domain, uint32_t mask)
{
	uint32_t prio[3] = {};
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
	}

	for (auto &p : prio)
	{
		for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
		{
			if ((1u << i) & mask)
			{
				uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
				if ((flags & p) == p)
					return i;
			}
		}
	}

	return UINT32_MAX;
}

uint32_t Device::find_memory_type(ImageDomain domain, uint32_t mask)
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
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & desired) == desired)
				return i;
		}
	}

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
	{
		if ((1u << i) & mask)
		{
			uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
			if ((flags & fallback) == fallback)
				return i;
		}
	}

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
		return VK_IMAGE_VIEW_TYPE_RANGE_SIZE;
	}
}

BufferViewHandle Device::create_buffer_view(const BufferViewCreateInfo &view_info)
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
	VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_RANGE_SIZE;
	vector<VkImageView> rt_views;
	DeviceAllocation allocation;
	DeviceAllocator *allocator = nullptr;
	bool owned = true;

	VkImageViewType get_default_view_type() const
	{
		return default_view_type;
	}

	bool setup_conversion_info(VkImageViewCreateInfo &create_info, VkSamplerYcbcrConversionInfo &conversion)
	{
		switch (create_info.format)
		{
		case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
			if (!device->get_device_features().sampler_ycbcr_conversion_features.samplerYcbcrConversion)
				return false;
			create_info.pNext = &conversion;
			conversion = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
			conversion.conversion = device->samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV420P_3PLANE)];
			break;

		case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
			if (!device->get_device_features().sampler_ycbcr_conversion_features.samplerYcbcrConversion)
				return false;
			create_info.pNext = &conversion;
			conversion = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
			conversion.conversion = device->samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV422P_3PLANE)];
			break;

		case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
			if (!device->get_device_features().sampler_ycbcr_conversion_features.samplerYcbcrConversion)
				return false;
			create_info.pNext = &conversion;
			conversion = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
			conversion.conversion = device->samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV444P_3PLANE)];
			break;

		default:
			break;
		}

		return true;
	}

	bool create_default_views(const ImageCreateInfo &create_info, const VkImageViewCreateInfo *view_info,
	                          bool create_unorm_srgb_views = false, const VkFormat *view_formats = nullptr)
	{
		VkDevice vkdevice = device->get_device();

		if ((create_info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) == 0)
		{
			LOGE("Cannot create image view unless certain usage flags are present.\n");
			return false;
		}

		VkImageViewCreateInfo default_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		VkSamplerYcbcrConversionInfo conversion_info = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };

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
		if (!setup_conversion_info(default_view_info, conversion_info))
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

			info.format = view_formats[0];
			if (table.vkCreateImageView(vkdevice, &info, nullptr, &unorm_view) != VK_SUCCESS)
				return false;

			info.format = view_formats[1];
			if (table.vkCreateImageView(vkdevice, &info, nullptr, &srgb_view) != VK_SUCCESS)
				return false;
		}

		return true;
	}

private:
	bool create_render_target_views(const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info)
	{
		rt_views.reserve(info.subresourceRange.layerCount);

		if (info.viewType == VK_IMAGE_VIEW_TYPE_3D)
			return true;

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
				// Sanity check. Don't want to implement layered views for this.
				if (info.subresourceRange.levelCount > 1)
				{
					LOGE("Cannot create depth stencil attachments with more than 1 mip level currently, and non-DS usage flags.\n");
					return false;
				}

				if (info.subresourceRange.layerCount > 1)
				{
					LOGE("Cannot create layered depth stencil attachments with non-DS usage flags.\n");
					return false;
				}

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

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = create_info.image->get_image();
	view_info.format = format;
	view_info.components = create_info.swizzle;
	view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
	view_info.subresourceRange.baseMipLevel = create_info.base_level;
	view_info.subresourceRange.baseArrayLayer = create_info.base_layer;
	view_info.subresourceRange.levelCount = create_info.levels;
	view_info.subresourceRange.layerCount = create_info.layers;

	if (create_info.view_type == VK_IMAGE_VIEW_TYPE_RANGE_SIZE)
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

	if (!holder.create_default_views(image_create_info, &view_info))
		return ImageViewHandle(nullptr);

	ImageViewCreateInfo tmp = create_info;
	tmp.format = format;
	ImageViewHandle ret(handle_pool.image_views.allocate(this, holder.image_view, tmp));
	if (ret)
	{
		holder.owned = false;
		ret->set_alt_views(holder.depth_view, holder.stencil_view);
		ret->set_render_target_views(move(holder.rt_views));
		return ret;
	}
	else
		return ImageViewHandle(nullptr);
}

#ifndef _WIN32
ImageHandle Device::create_imported_image(int fd, VkDeviceSize size, uint32_t memory_type,
                                          VkExternalMemoryHandleTypeFlagBitsKHR handle_type,
                                          const ImageCreateInfo &create_info)
{
	if (!ext.supports_external)
		return {};

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
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = create_info.usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.flags = create_info.flags;
	VK_ASSERT(create_info.domain != ImageDomain::Transient);

	VkExternalMemoryImageCreateInfoKHR externalInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR };
	externalInfo.handleTypes = handle_type;
	info.pNext = &externalInfo;

	VK_ASSERT(image_format_is_supported(create_info.format, image_usage_to_features(info.usage), info.tiling));

	if (table->vkCreateImage(device, &info, nullptr, &holder.image) != VK_SUCCESS)
		return ImageHandle(nullptr);

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = size;
	alloc_info.memoryTypeIndex = memory_type;

	VkMemoryDedicatedAllocateInfoKHR dedicated_info = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR };
	dedicated_info.image = holder.image;
	alloc_info.pNext = &dedicated_info;

	VkImportMemoryFdInfoKHR fd_info = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
	fd_info.handleType = handle_type;
	fd_info.fd = fd;
	dedicated_info.pNext = &fd_info;

	VkMemoryRequirements reqs;
	table->vkGetImageMemoryRequirements(device, holder.image, &reqs);
	if (reqs.size > size)
		return ImageHandle(nullptr);

	if (((1u << memory_type) & reqs.memoryTypeBits) == 0)
		return ImageHandle(nullptr);

	if (table->vkAllocateMemory(device, &alloc_info, nullptr, &holder.memory) != VK_SUCCESS)
		return ImageHandle(nullptr);

	if (table->vkBindImageMemory(device, holder.image, holder.memory, 0) != VK_SUCCESS)
		return ImageHandle(nullptr);

	// Create default image views.
	// App could of course to this on its own, but it's very handy to have these being created automatically for you.
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_RANGE_SIZE;
	if (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
	{
		if (!holder.create_default_views(create_info, nullptr))
			return ImageHandle(nullptr);
		view_type = holder.get_default_view_type();
	}

	auto allocation = DeviceAllocation::make_imported_allocation(holder.memory, size, memory_type);
	ImageHandle handle(handle_pool.images.allocate(this, holder.image, holder.image_view, allocation, create_info, view_type));
	if (handle)
	{
		holder.owned = false;
		handle->get_view().set_alt_views(holder.depth_view, holder.stencil_view);
		handle->get_view().set_render_target_views(move(holder.rt_views));

		// Set possible dstStage and dstAccess.
		handle->set_stage_flags(image_usage_to_possible_stages(info.usage));
		handle->set_access_flags(image_usage_to_possible_access(info.usage));
		return handle;
	}
	else
		return ImageHandle(nullptr);
}
#endif

InitialImageBuffer Device::create_image_staging_buffer(const TextureFormatLayout &layout)
{
	InitialImageBuffer result;

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Host;
	buffer_info.size = layout.get_required_size();
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	result.buffer = create_buffer(buffer_info, nullptr);
	set_name(*result.buffer, "image-upload-staging-buffer");

	auto *mapped = static_cast<uint8_t *>(map_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
	memcpy(mapped, layout.data(), layout.get_required_size());
	unmap_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);

	layout.build_buffer_image_copies(result.blits);
	return result;
}

InitialImageBuffer Device::create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial)
{
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

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Host;
	buffer_info.size = layout.get_required_size();
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	result.buffer = create_buffer(buffer_info, nullptr);
	set_name(*result.buffer, "image-upload-staging-buffer");

	// And now, do the actual copy.
	auto *mapped = static_cast<uint8_t *>(map_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
	unsigned index = 0;

	layout.set_buffer(mapped, layout.get_required_size());

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

			uint8_t *dst = static_cast<uint8_t *>(layout.data(layer, level));
			const uint8_t *src = static_cast<const uint8_t *>(initial[index].data);

			for (uint32_t z = 0; z < mip_info.depth; z++)
				for (uint32_t y = 0; y < mip_info.block_image_height; y++)
					memcpy(dst + z * dst_height_stride + y * row_size, src + z * src_height_stride + y * src_row_stride, row_size);
		}
	}

	unmap_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);
	layout.build_buffer_image_copies(result.blits);
	return result;
}

YCbCrImageHandle Device::create_ycbcr_image(const YCbCrImageCreateInfo &create_info)
{
	if (!ext.sampler_ycbcr_conversion_features.samplerYcbcrConversion)
		return YCbCrImageHandle(nullptr);

	VkFormatProperties format_properties = {};
	get_format_properties(format_ycbcr_planar_vk_format(create_info.format), &format_properties);

	if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DISJOINT_BIT) == 0)
	{
		LOGE("YCbCr format does not support DISJOINT_BIT.\n");
		return YCbCrImageHandle(nullptr);
	}

	if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT) == 0)
	{
		LOGE("YCbCr format does not support MIDPOINT_CHROMA_SAMPLES_BIT.\n");
		return YCbCrImageHandle(nullptr);
	}

	if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) == 0)
	{
		LOGE("YCbCr format does not support YCBCR_CONVERSION_LINEAR_FILTER_BIT.\n");
		return YCbCrImageHandle(nullptr);
	}

	ImageHandle ycbcr_image;
	ImageHandle plane_handles[3];
	unsigned num_planes = format_ycbcr_num_planes(create_info.format);

	for (unsigned i = 0; i < num_planes; i++)
	{
		ImageCreateInfo plane_info = ImageCreateInfo::immutable_2d_image(
				create_info.width,
				create_info.height,
				format_ycbcr_plane_vk_format(create_info.format, i));
		plane_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		plane_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;

		plane_info.width >>= format_ycbcr_downsample_ratio_log2(create_info.format, 0, i);
		plane_info.height >>= format_ycbcr_downsample_ratio_log2(create_info.format, 1, i);
		plane_info.flags = VK_IMAGE_CREATE_ALIAS_BIT; // Will alias directly over the YCbCr image.
		plane_info.misc = IMAGE_MISC_FORCE_NO_DEDICATED_BIT;
		plane_handles[i] = create_image(plane_info);
		if (!plane_handles[i])
		{
			LOGE("Failed to create plane image.\n");
			return YCbCrImageHandle(nullptr);
		}
	}

	ImageCreateInfo ycbcr_info = ImageCreateInfo::immutable_2d_image(
			create_info.width,
			create_info.height,
			format_ycbcr_planar_vk_format(create_info.format));
	ycbcr_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	ycbcr_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	ycbcr_info.flags = VK_IMAGE_CREATE_DISJOINT_BIT | VK_IMAGE_CREATE_ALIAS_BIT;
	ycbcr_info.misc = IMAGE_MISC_FORCE_NO_DEDICATED_BIT;

	const DeviceAllocation *allocations[3];
	for (unsigned i = 0; i < num_planes; i++)
		allocations[i] = &plane_handles[i]->get_allocation();
	ycbcr_info.memory_aliases = allocations;
	ycbcr_info.num_memory_aliases = num_planes;

	ycbcr_image = create_image(ycbcr_info);
	if (!ycbcr_image)
		return YCbCrImageHandle(nullptr);

	YCbCrImageHandle handle(handle_pool.ycbcr_images.allocate(this, create_info.format, ycbcr_image, plane_handles, num_planes));
	return handle;
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
                                   VkImage image, VkImageTiling tiling)
{
	if ((info.flags & VK_IMAGE_CREATE_DISJOINT_BIT) != 0 && info.num_memory_aliases == 0)
	{
		LOGE("Must use memory aliases when creating a DISJOINT planar image.\n");
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
			if (!ext.supports_bind_memory2 || !ext.supports_get_memory_requirements2)
				return false;

			VkBindImageMemoryInfo bind_infos[3];
			VkBindImagePlaneMemoryInfo bind_plane_infos[3];
			VK_ASSERT(num_planes <= 3);

			for (unsigned plane = 0; plane < num_planes; plane++)
			{
				VkMemoryRequirements2KHR memory_req = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR };
				VkImageMemoryRequirementsInfo2KHR image_info = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR };
				image_info.image = image;

				VkImagePlaneMemoryRequirementsInfo plane_info = { VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO_KHR };
				plane_info.planeAspect = static_cast<VkImageAspectFlagBits>(VK_IMAGE_ASPECT_PLANE_0_BIT << plane);
				image_info.pNext = &plane_info;

				table->vkGetImageMemoryRequirements2KHR(device, &image_info, &memory_req);
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

			if (table->vkBindImageMemory2KHR(device, num_planes, bind_infos) != VK_SUCCESS)
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

		uint32_t memory_type = find_memory_type(info.domain, reqs.memoryTypeBits);
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

		if (!managers.memory.allocate_image_memory(reqs.size, reqs.alignment, memory_type,
		                                           tiling == VK_IMAGE_TILING_OPTIMAL ? ALLOCATION_TILING_OPTIMAL
		                                                                             : ALLOCATION_TILING_LINEAR,
		                                           allocation, image,
		                                           (info.misc & IMAGE_MISC_FORCE_NO_DEDICATED_BIT) != 0))
		{
			LOGE("Failed to allocate image memory (type %u, size: %u).\n", unsigned(memory_type), unsigned(reqs.size));
			return false;
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

	if (create_info.domain == ImageDomain::LinearHostCached || create_info.domain == ImageDomain::LinearHost)
	{
		info.tiling = VK_IMAGE_TILING_LINEAR;
		info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	}
	else
	{
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	info.usage = create_info.usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (create_info.domain == ImageDomain::Transient)
		info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	if (staging_buffer)
		info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	info.flags = create_info.flags;

	if (info.mipLevels == 0)
		info.mipLevels = image_num_miplevels(info.extent);

	VkImageFormatListCreateInfoKHR format_info = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR };
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
			if (ext.supports_image_format_list)
				info.pNext = &format_info;
		}
	}

	if ((create_info.usage & VK_IMAGE_USAGE_STORAGE_BIT) ||
	    (create_info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT))
	{
		info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	}

	// Only do this conditionally.
	// On AMD, using CONCURRENT with async compute disables compression.
	uint32_t sharing_indices[3] = {};

	uint32_t queue_flags = create_info.misc & (IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT |
	                                           IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT |
	                                           IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT |
	                                           IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT);
	bool concurrent_queue = queue_flags != 0;
	if (concurrent_queue)
	{
		info.sharingMode = VK_SHARING_MODE_CONCURRENT;

		const auto add_unique_family = [&](uint32_t family) {
			for (uint32_t i = 0; i < info.queueFamilyIndexCount; i++)
			{
				if (sharing_indices[i] == family)
					return;
			}
			sharing_indices[info.queueFamilyIndexCount++] = family;
		};

		if (queue_flags & (IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT))
			add_unique_family(graphics_queue_family_index);
		if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT)
			add_unique_family(compute_queue_family_index);
		if (staging_buffer || (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT) != 0)
			add_unique_family(transfer_queue_family_index);

		if (info.queueFamilyIndexCount > 1)
			info.pQueueFamilyIndices = sharing_indices;
		else
		{
			info.pQueueFamilyIndices = nullptr;
			info.queueFamilyIndexCount = 0;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}
	}

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

		VkImageFormatProperties props;
		if (!get_image_format_properties(info.format, info.imageType, info.tiling, info.usage, info.flags, &props))
			return ImageHandle(nullptr);

		if (!props.maxArrayLayers ||
		    !props.maxMipLevels ||
		    (info.extent.width > props.maxExtent.width) ||
		    (info.extent.height > props.maxExtent.height) ||
		    (info.extent.depth > props.maxExtent.depth))
		{
			return ImageHandle(nullptr);
		}
	}

	if (!image_format_is_supported(create_info.format, image_usage_to_features(info.usage) | check_extra_features, info.tiling))
	{
		LOGE("Format %u is not supported for usage flags!\n", unsigned(create_info.format));
		return ImageHandle(nullptr);
	}

	if (table->vkCreateImage(device, &info, nullptr, &holder.image) != VK_SUCCESS)
	{
		LOGE("Failed to create image in vkCreateImage.\n");
		return ImageHandle(nullptr);
	}

	if (!allocate_image_memory(&holder.allocation, create_info, holder.image, info.tiling))
	{
		LOGE("Failed to allocate memory for image.\n");
		return ImageHandle(nullptr);
	}

	auto tmpinfo = create_info;
	tmpinfo.usage = info.usage;
	tmpinfo.flags = info.flags;
	tmpinfo.levels = info.mipLevels;

	bool has_view = (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) != 0;

	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_RANGE_SIZE;
	if (has_view)
	{
		if (!holder.create_default_views(tmpinfo, nullptr, create_unorm_srgb_views, view_formats))
			return ImageHandle(nullptr);
		view_type = holder.get_default_view_type();
	}

	ImageHandle handle(handle_pool.images.allocate(this, holder.image, holder.image_view, holder.allocation, tmpinfo, view_type));
	if (handle)
	{
		holder.owned = false;
		if (has_view)
		{
			handle->get_view().set_alt_views(holder.depth_view, holder.stencil_view);
			handle->get_view().set_render_target_views(move(holder.rt_views));
			handle->get_view().set_unorm_view(holder.unorm_view);
			handle->get_view().set_srgb_view(holder.srgb_view);
		}

		// Set possible dstStage and dstAccess.
		handle->set_stage_flags(image_usage_to_possible_stages(info.usage));
		handle->set_access_flags(image_usage_to_possible_access(info.usage));
	}

	// Copy initial data to texture.
	if (staging_buffer)
	{
		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);
		bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;

		// If graphics_queue != transfer_queue, we will use a semaphore, so no srcAccess mask is necessary.
		VkAccessFlags final_transition_src_access = 0;
		if (generate_mips)
			final_transition_src_access = VK_ACCESS_TRANSFER_READ_BIT; // Validation complains otherwise.
		else if (graphics_queue == transfer_queue)
			final_transition_src_access = VK_ACCESS_TRANSFER_WRITE_BIT;

		VkAccessFlags prepare_src_access = graphics_queue == transfer_queue ? VK_ACCESS_TRANSFER_WRITE_BIT : 0;
		bool need_mipmap_barrier = true;
		bool need_initial_barrier = true;

		// Now we've used the TRANSFER queue to copy data over to the GPU.
		// For mipmapping, we're now moving over to graphics,
		// the transfer queue is designed for CPU <-> GPU and that's it.

		// For concurrent queue mode, we just need to inject a semaphore.
		// For non-concurrent queue mode, we will have to inject ownership transfer barrier if the queue families do not match.

		auto graphics_cmd = request_command_buffer(CommandBuffer::Type::Generic);
		CommandBufferHandle transfer_cmd;

		// Don't split the upload into multiple command buffers unless we have to.
		if (transfer_queue != graphics_queue)
			transfer_cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		else
			transfer_cmd = graphics_cmd;

		transfer_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                            VK_ACCESS_TRANSFER_WRITE_BIT);

		transfer_cmd->begin_region("copy-image-to-gpu");
		transfer_cmd->copy_buffer_to_image(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());
		transfer_cmd->end_region();

		if (transfer_queue != graphics_queue)
		{
			VkPipelineStageFlags dst_stages =
					generate_mips ? VkPipelineStageFlags(VK_PIPELINE_STAGE_TRANSFER_BIT) : handle->get_stage_flags();

			// We can't just use semaphores, we will also need a release + acquire barrier to marshal ownership from
			// transfer queue over to graphics ...
			if (!concurrent_queue && transfer_queue_family_index != graphics_queue_family_index)
			{
				need_mipmap_barrier = false;

				VkImageMemoryBarrier release = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				release.image = handle->get_image();
				release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				release.dstAccessMask = 0;
				release.srcQueueFamilyIndex = transfer_queue_family_index;
				release.dstQueueFamilyIndex = graphics_queue_family_index;
				release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

				if (generate_mips)
				{
					release.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					release.subresourceRange.levelCount = 1;
				}
				else
				{
					release.newLayout = create_info.initial_layout;
					release.subresourceRange.levelCount = info.mipLevels;
					need_initial_barrier = false;
				}

				release.subresourceRange.aspectMask = format_to_aspect_mask(info.format);
				release.subresourceRange.layerCount = info.arrayLayers;

				VkImageMemoryBarrier acquire = release;
				acquire.srcAccessMask = 0;

				if (generate_mips)
					acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				else
					acquire.dstAccessMask = handle->get_access_flags() & image_layout_to_possible_access(create_info.initial_layout);

				transfer_cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
				                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				                      0, nullptr, 0, nullptr, 1, &release);

				graphics_cmd->barrier(dst_stages,
				                      dst_stages,
				                      0, nullptr, 0, nullptr, 1, &acquire);
			}

			Semaphore sem;
			submit(transfer_cmd, nullptr, 1, &sem);
			add_wait_semaphore(CommandBuffer::Type::Generic, sem, dst_stages, true);
		}

		if (generate_mips)
		{
			graphics_cmd->begin_region("mipgen");
			graphics_cmd->barrier_prepare_generate_mipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                                              VK_PIPELINE_STAGE_TRANSFER_BIT,
			                                              prepare_src_access, need_mipmap_barrier);
			graphics_cmd->generate_mipmap(*handle);
			graphics_cmd->end_region();
		}

		if (need_initial_barrier)
		{
			graphics_cmd->image_barrier(
					*handle, generate_mips ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					create_info.initial_layout,
					VK_PIPELINE_STAGE_TRANSFER_BIT, final_transition_src_access,
					handle->get_stage_flags(),
					handle->get_access_flags() & image_layout_to_possible_access(create_info.initial_layout));
		}

		bool share_compute = concurrent_queue && graphics_queue != compute_queue;
		bool share_async_graphics = get_physical_queue_type(CommandBuffer::Type::AsyncGraphics) == CommandBuffer::Type::AsyncCompute;

		// For concurrent queue, make sure that compute can see the final image as well.
		// Also add semaphore if the compute queue can be used for async graphics as well.
		if (share_compute || share_async_graphics)
		{
			Semaphore sem;
			submit(graphics_cmd, nullptr, 1, &sem);

			VkPipelineStageFlags dst_stages = handle->get_stage_flags();
			if (graphics_queue_family_index != compute_queue_family_index)
				dst_stages &= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
			add_wait_semaphore(CommandBuffer::Type::AsyncCompute, sem, dst_stages, true);
		}
		else
			submit(graphics_cmd);
	}
	else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
	{
		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		auto cmd = request_command_buffer(CommandBuffer::Type::Generic);
		cmd->image_barrier(*handle, info.initialLayout, create_info.initial_layout,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, handle->get_stage_flags(),
		                   handle->get_access_flags() &
		                   image_layout_to_possible_access(create_info.initial_layout));

		// For concurrent queue, make sure that compute can see the final image as well.
		if (concurrent_queue && graphics_queue != compute_queue)
		{
			Semaphore sem;
			submit(cmd, nullptr, 1, &sem);
			add_wait_semaphore(CommandBuffer::Type::AsyncCompute,
			                   sem, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
		}
		else
			submit(cmd);
	}

	return handle;
}

static VkSamplerCreateInfo fill_vk_sampler_info(const SamplerCreateInfo &sampler_info)
{
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	info.magFilter = sampler_info.mag_filter;
	info.minFilter = sampler_info.min_filter;
	info.mipmapMode = sampler_info.mipmap_mode;
	info.addressModeU = sampler_info.address_mode_u;
	info.addressModeV = sampler_info.address_mode_v;
	info.addressModeW = sampler_info.address_mode_w;
	info.mipLodBias = sampler_info.mip_lod_bias;
	info.anisotropyEnable = sampler_info.anisotropy_enable;
	info.maxAnisotropy = sampler_info.max_anisotropy;
	info.compareEnable = sampler_info.compare_enable;
	info.compareOp = sampler_info.compare_op;
	info.minLod = sampler_info.min_lod;
	info.maxLod = sampler_info.max_lod;
	info.borderColor = sampler_info.border_color;
	info.unnormalizedCoordinates = sampler_info.unnormalized_coordinates;
	return info;
}

SamplerHandle Device::create_sampler(const SamplerCreateInfo &sampler_info, StockSampler stock_sampler)
{
	auto info = fill_vk_sampler_info(sampler_info);
	VkSampler sampler;

	VkSamplerYcbcrConversionInfo conversion_info = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };

	switch (stock_sampler)
	{
	case StockSampler::LinearYUV420P:
		if (!ext.sampler_ycbcr_conversion_features.samplerYcbcrConversion)
			return SamplerHandle(nullptr);
		info.pNext = &conversion_info;
		conversion_info.conversion = samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV420P_3PLANE)];
		break;

	case StockSampler::LinearYUV422P:
		if (!ext.sampler_ycbcr_conversion_features.samplerYcbcrConversion)
			return SamplerHandle(nullptr);
		info.pNext = &conversion_info;
		conversion_info.conversion = samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV422P_3PLANE)];
		break;

	case StockSampler::LinearYUV444P:
		if (!ext.sampler_ycbcr_conversion_features.samplerYcbcrConversion)
			return SamplerHandle(nullptr);
		info.pNext = &conversion_info;
		conversion_info.conversion = samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::YUV444P_3PLANE)];
		break;

	default:
		info.pNext = nullptr;
		break;
	}

	if (table->vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
		return SamplerHandle(nullptr);
#ifdef GRANITE_VULKAN_FOSSILIZE
	register_sampler(sampler, Fossilize::Hash(stock_sampler) | 0x10000, info);
#else
	(void)stock_sampler;
#endif
	SamplerHandle handle(handle_pool.samplers.allocate(this, sampler, sampler_info));
	handle->set_internal_sync_object();
	return handle;
}

SamplerHandle Device::create_sampler(const SamplerCreateInfo &sampler_info)
{
	auto info = fill_vk_sampler_info(sampler_info);
	VkSampler sampler;
	if (table->vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
		return SamplerHandle(nullptr);
	return SamplerHandle(handle_pool.samplers.allocate(this, sampler, sampler_info));
}

BindlessDescriptorPoolHandle Device::create_bindless_descriptor_pool(BindlessResourceType type,
                                                                     unsigned num_sets, unsigned num_descriptors)
{
	if (!ext.supports_descriptor_indexing)
		return BindlessDescriptorPoolHandle{ nullptr };

	DescriptorSetAllocator *allocator = nullptr;

	switch (type)
	{
	case BindlessResourceType::ImageFP:
		allocator = bindless_sampled_image_allocator_fp;
		break;

	case BindlessResourceType::ImageInt:
		allocator = bindless_sampled_image_allocator_integer;
		break;

	default:
		break;
	}

	VkDescriptorPool pool = VK_NULL_HANDLE;
	if (allocator)
		pool = allocator->allocate_bindless_pool(num_sets, num_descriptors);

	if (!pool)
	{
		LOGE("Failed to allocate bindless pool.\n");
		return BindlessDescriptorPoolHandle{ nullptr };
	}

	auto *handle = handle_pool.bindless_descriptor_pool.allocate(this, allocator, pool);
	return BindlessDescriptorPoolHandle{ handle };
}

void Device::fill_buffer_sharing_indices(VkBufferCreateInfo &info, uint32_t *sharing_indices)
{
	if (graphics_queue_family_index != compute_queue_family_index ||
	    graphics_queue_family_index != transfer_queue_family_index)
	{
		// For buffers, always just use CONCURRENT access modes,
		// so we don't have to deal with acquire/release barriers in async compute.
		info.sharingMode = VK_SHARING_MODE_CONCURRENT;

		sharing_indices[info.queueFamilyIndexCount++] = graphics_queue_family_index;

		if (graphics_queue_family_index != compute_queue_family_index)
			sharing_indices[info.queueFamilyIndexCount++] = compute_queue_family_index;

		if (graphics_queue_family_index != transfer_queue_family_index &&
		    compute_queue_family_index != transfer_queue_family_index)
		{
			sharing_indices[info.queueFamilyIndexCount++] = transfer_queue_family_index;
		}

		info.pQueueFamilyIndices = sharing_indices;
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

	VkMemoryHostPointerPropertiesEXT host_pointer_props = { VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT };
	if (table->vkGetMemoryHostPointerPropertiesEXT(device, type, host_buffer, &host_pointer_props) != VK_SUCCESS)
	{
		LOGE("Host pointer is not importable.\n");
		return BufferHandle{};
	}

	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	info.size = create_info.size;
	info.usage = create_info.usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	uint32_t sharing_indices[3];
	fill_buffer_sharing_indices(info, sharing_indices);

	VkBuffer buffer;
	VkMemoryRequirements reqs;
	if (table->vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return BufferHandle{};

	table->vkGetBufferMemoryRequirements(device, buffer, &reqs);

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
		LOGE("Failed to find memory type.\n");
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = (create_info.size + ext.host_memory_properties.minImportedHostPointerAlignment - 1) &
	                            ~(ext.host_memory_properties.minImportedHostPointerAlignment - 1);
	alloc_info.memoryTypeIndex = memory_type;

	VkImportMemoryHostPointerInfoEXT import = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT };
	import.handleType = type;
	import.pHostPointer = host_buffer;
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
		allocation.free_immediate(managers.memory);
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	if (table->vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS)
	{
		allocation.free_immediate(managers.memory);
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle{};
	}

	BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, create_info));
	return handle;
}

BufferHandle Device::create_buffer(const BufferCreateInfo &create_info, const void *initial)
{
	VkBuffer buffer;
	VkMemoryRequirements reqs;
	DeviceAllocation allocation;

	bool zero_initialize = (create_info.misc & BUFFER_MISC_ZERO_INITIALIZE_BIT) != 0;
	if (initial && zero_initialize)
	{
		LOGE("Cannot initialize buffer with data and clear.\n");
		return BufferHandle{};
	}

	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	info.size = create_info.size;
	info.usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	uint32_t sharing_indices[3];
	fill_buffer_sharing_indices(info, sharing_indices);

	if (table->vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return BufferHandle(nullptr);

	table->vkGetBufferMemoryRequirements(device, buffer, &reqs);

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	if (memory_type == UINT32_MAX)
	{
		LOGE("Failed to find memory type.\n");
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	if (!managers.memory.allocate(reqs.size, reqs.alignment, memory_type, ALLOCATION_TILING_LINEAR, &allocation))
	{
		// This memory type is rather scarce, so fallback to Host type if we've exhausted this memory.
		if (create_info.domain == BufferDomain::LinkedDeviceHost)
		{
			LOGW("Exhausted LinkedDeviceHost memory, falling back to host.\n");
			memory_type = find_memory_type(BufferDomain::Host, reqs.memoryTypeBits);
			if (memory_type == UINT32_MAX)
			{
				LOGE("Failed to find memory type.\n");
				table->vkDestroyBuffer(device, buffer, nullptr);
				return BufferHandle(nullptr);
			}

			if (!managers.memory.allocate(reqs.size, reqs.alignment, memory_type, ALLOCATION_TILING_LINEAR, &allocation))
			{
				table->vkDestroyBuffer(device, buffer, nullptr);
				return BufferHandle(nullptr);
			}
		}
		else
		{
			table->vkDestroyBuffer(device, buffer, nullptr);
			return BufferHandle(nullptr);
		}
	}

	if (table->vkBindBufferMemory(device, buffer, allocation.get_memory(), allocation.get_offset()) != VK_SUCCESS)
	{
		allocation.free_immediate(managers.memory);
		table->vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	auto tmpinfo = create_info;
	tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, tmpinfo));

	if (create_info.domain == BufferDomain::Device && (initial || zero_initialize) && !memory_type_is_host_visible(memory_type))
	{
		CommandBufferHandle cmd;
		if (initial)
		{
			auto staging_info = create_info;
			staging_info.domain = BufferDomain::Host;
			auto staging_buffer = create_buffer(staging_info, initial);
			set_name(*staging_buffer, "buffer-upload-staging-buffer");

			cmd = request_command_buffer(CommandBuffer::Type::AsyncTransfer);
			cmd->begin_region("copy-buffer-staging");
			cmd->copy_buffer(*handle, *staging_buffer);
			cmd->end_region();
		}
		else
		{
			cmd = request_command_buffer(CommandBuffer::Type::AsyncCompute);
			cmd->begin_region("fill-buffer-staging");
			cmd->fill_buffer(*handle, 0);
			cmd->end_region();
		}

		LOCK();
		submit_staging(cmd, info.usage, true);
	}
	else if (initial || zero_initialize)
	{
		void *ptr = managers.memory.map_memory(allocation, MEMORY_ACCESS_WRITE_BIT, 0, allocation.get_size());
		if (!ptr)
			return BufferHandle(nullptr);

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

void Device::get_format_properties(VkFormat format, VkFormatProperties *properties)
{
	vkGetPhysicalDeviceFormatProperties(gpu, format, properties);
}

bool Device::get_image_format_properties(VkFormat format, VkImageType type, VkImageTiling tiling,
                                         VkImageUsageFlags usage, VkImageCreateFlags flags,
                                         VkImageFormatProperties *properties)
{
	auto res = vkGetPhysicalDeviceImageFormatProperties(gpu, format, type, tiling, usage, flags,
	                                                    properties);
	return res == VK_SUCCESS;
}

bool Device::image_format_is_supported(VkFormat format, VkFormatFeatureFlags required, VkImageTiling tiling) const
{
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
	auto flags = tiling == VK_IMAGE_TILING_OPTIMAL ? props.optimalTilingFeatures : props.linearTilingFeatures;
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
#ifdef GRANITE_VULKAN_MT
	return cookie.fetch_add(16, memory_order_relaxed) + 16;
#else
	cookie += 16;
	return cookie;
#endif
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

ImageView &Device::get_transient_attachment(unsigned width, unsigned height, VkFormat format,
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
		info.depth_stencil =
		    &get_transient_attachment(wsi.swapchain[wsi.index]->get_create_info().width,
		                              wsi.swapchain[wsi.index]->get_create_info().height, get_default_depth_format());
		break;
	}

	case SwapchainRenderPass::DepthStencil:
	{
		info.op_flags |= RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
		info.depth_stencil =
		    &get_transient_attachment(wsi.swapchain[wsi.index]->get_create_info().width,
		                              wsi.swapchain[wsi.index]->get_create_info().height, get_default_depth_stencil_format());
		break;
	}

	default:
		break;
	}
	return info;
}

void Device::set_queue_lock(std::function<void()> lock_callback, std::function<void()> unlock_callback)
{
	queue_lock_callback = move(lock_callback);
	queue_unlock_callback = move(unlock_callback);
}

void Device::set_name(const Buffer &buffer, const char *name)
{
	if (ext.supports_debug_utils)
	{
		VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_OBJECT_TYPE_BUFFER;
		info.objectHandle = (uint64_t)buffer.get_buffer();
		info.pObjectName = name;
		if (vkSetDebugUtilsObjectNameEXT)
			vkSetDebugUtilsObjectNameEXT(device, &info);
	}
	else if (ext.supports_debug_marker)
	{
		VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT;
		info.object = (uint64_t)buffer.get_buffer();
		info.pObjectName = name;
		table->vkDebugMarkerSetObjectNameEXT(device, &info);
	}
}

void Device::set_name(const Image &image, const char *name)
{
	if (ext.supports_debug_utils)
	{
		VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_OBJECT_TYPE_IMAGE;
		info.objectHandle = (uint64_t)image.get_image();
		info.pObjectName = name;
		if (vkSetDebugUtilsObjectNameEXT)
			vkSetDebugUtilsObjectNameEXT(device, &info);
	}
	else if (ext.supports_debug_marker)
	{
		VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
		info.object = (uint64_t)image.get_image();
		info.pObjectName = name;
		table->vkDebugMarkerSetObjectNameEXT(device, &info);
	}
}

void Device::set_name(const CommandBuffer &cmd, const char *name)
{
	if (ext.supports_debug_utils)
	{
		VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_OBJECT_TYPE_COMMAND_BUFFER;
		info.objectHandle = (uint64_t)cmd.get_command_buffer();
		info.pObjectName = name;
		if (vkSetDebugUtilsObjectNameEXT)
			vkSetDebugUtilsObjectNameEXT(device, &info);
	}
	else if (ext.supports_debug_marker)
	{
		VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
		info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT;
		info.object = (uint64_t)cmd.get_command_buffer();
		info.pObjectName = name;
		table->vkDebugMarkerSetObjectNameEXT(device, &info);
	}
}

void Device::report_checkpoints()
{
	if (!ext.supports_nv_device_diagnostic_checkpoints)
		return;

	uint32_t graphics_count;
	table->vkGetQueueCheckpointDataNV(graphics_queue, &graphics_count, nullptr);
	vector<VkCheckpointDataNV> graphics_data(graphics_count);
	for (auto &g : graphics_data)
		g.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
	table->vkGetQueueCheckpointDataNV(graphics_queue, &graphics_count, graphics_data.data());

	uint32_t compute_count;
	table->vkGetQueueCheckpointDataNV(compute_queue, &compute_count, nullptr);
	vector<VkCheckpointDataNV> compute_data(compute_count);
	for (auto &g : compute_data)
		g.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
	table->vkGetQueueCheckpointDataNV(compute_queue, &compute_count, compute_data.data());

	uint32_t transfer_count;
	table->vkGetQueueCheckpointDataNV(transfer_queue, &transfer_count, nullptr);
	vector<VkCheckpointDataNV> transfer_data(compute_count);
	for (auto &g : transfer_data)
		g.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
	table->vkGetQueueCheckpointDataNV(transfer_queue, &transfer_count, transfer_data.data());

	if (!graphics_data.empty())
	{
		LOGI("Checkpoints for graphics queue:\n");
		for (auto &g : graphics_data)
			LOGI("Stage %u:\n%s\n", g.stage, static_cast<const char *>(g.pCheckpointMarker));
	}

	if (!compute_data.empty())
	{
		LOGI("Checkpoints for compute queue:\n");
		for (auto &g : compute_data)
			LOGI("    Stage %u:\n%s\n", g.stage, static_cast<const char *>(g.pCheckpointMarker));
	}

	if (!transfer_data.empty())
	{
		LOGI("Checkpoints for transfer queue:\n");
		for (auto &g : transfer_data)
			LOGI("    Stage %u:\n%s\n", g.stage, static_cast<const char *>(g.pCheckpointMarker));
	}
}

void Device::query_available_performance_counters(CommandBuffer::Type type, uint32_t *count,
                                                  const VkPerformanceCounterKHR **counters,
                                                  const VkPerformanceCounterDescriptionKHR **desc)
{
	auto &query_pool = get_performance_query_pool(type);
	*count = query_pool.get_num_counters();
	*counters = query_pool.get_available_counters();
	*desc = query_pool.get_available_counter_descs();
}

bool Device::init_performance_counters(const std::vector<std::string> &names)
{
	if (!graphics.performance_query_pool.init_counters(names))
		return false;

	if (compute_queue_family_index != graphics_queue_family_index &&
	    !compute.performance_query_pool.init_counters(names))
	{
		return false;
	}

	if (transfer_queue_family_index != compute_queue_family_index &&
	    transfer_queue_family_index != graphics_queue_family_index &&
	    !transfer.performance_query_pool.init_counters(names))
	{
		return false;
	}

	return true;
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

double Device::convert_timestamp_delta(uint64_t start_ticks, uint64_t end_ticks) const
{
	int64_t ticks_delta = convert_to_signed_delta(start_ticks, end_ticks, timestamp_valid_bits);
	return double(int64_t(ticks_delta)) * gpu_props.limits.timestampPeriod * 1e-9;
}

uint64_t Device::update_wrapped_base_timestamp(uint64_t end_ticks)
{
	json_base_timestamp_value += convert_to_signed_delta(json_base_timestamp_value, end_ticks, timestamp_valid_bits);
	return json_base_timestamp_value;
}

bool Device::init_timestamp_trace(const char *path)
{
	// Use the Chrome tracing format. It's trivial to emit and we get a frontend for free :)
	json_trace_file.reset();
	json_trace_file.reset(fopen(path, "w"));
	if (json_trace_file)
		fprintf(json_trace_file.get(), "[");
	return bool(json_trace_file);
}

int64_t Device::convert_timestamp_to_absolute_usec(uint64_t ts)
{
	// Ensure that we deal with timestamp wraparound correctly.
	// On some hardware, we have < 64 valid bits and the timestamp counters will wrap around at some interval.
	// As long as timestamps come in at a reasonably steady pace, we can deal with wraparound cleanly.
	ts = update_wrapped_base_timestamp(ts);
	if (json_timestamp_origin == 0)
		json_timestamp_origin = ts;

	auto delta_ts = int64_t(ts - json_timestamp_origin);
	auto us = int64_t(double(int64_t(delta_ts)) * gpu_props.limits.timestampPeriod * 1e-3);
	return us;
}

void Device::write_json_timestamp_range(unsigned frame_index, const char *tid,
                                        const char *name, const char *extra,
                                        uint64_t start_ts, uint64_t end_ts,
                                        int64_t &min_us, int64_t &max_us)
{
	if (!json_trace_file)
		return;

	int64_t absolute_start = convert_timestamp_to_absolute_usec(start_ts);
	int64_t absolute_end = convert_timestamp_to_absolute_usec(end_ts);

	VK_ASSERT(absolute_start <= absolute_end);

	min_us = std::min(absolute_start, min_us);
	max_us = std::max(absolute_end, max_us);

	fprintf(json_trace_file.get(), "\t{ \"name\": \"%s%s%s\", \"ph\": \"B\", \"tid\": \"%s\", \"pid\": \"%u\", \"ts\": %lld },\n",
	        name, *extra != '\0' ? " " : "", extra, tid, frame_index, static_cast<long long>(absolute_start));
	fprintf(json_trace_file.get(), "\t{ \"name\": \"%s%s%s\", \"ph\": \"E\", \"tid\": \"%s\", \"pid\": \"%u\", \"ts\": %lld },\n",
	        name, *extra != '\0' ? " " : "", extra, tid, frame_index, static_cast<long long>(absolute_end));
}

void Device::write_json_timestamp_range_us(unsigned frame_index, const char *tid, const char *name, int64_t start_us, int64_t end_us)
{
	if (!json_trace_file)
		return;
	if (start_us > end_us)
		return;

	fprintf(json_trace_file.get(), "\t{ \"name\": \"%s\", \"ph\": \"B\", \"tid\": \"%s\", \"pid\": \"%u\", \"ts\": %lld },\n",
	        name, tid, frame_index, static_cast<long long>(start_us));
	fprintf(json_trace_file.get(), "\t{ \"name\": \"%s\", \"ph\": \"E\", \"tid\": \"%s\", \"pid\": \"%u\", \"ts\": %lld },\n",
	        name, tid, frame_index, static_cast<long long>(end_us));
}

void Device::JSONTraceFileDeleter::operator()(FILE *file)
{
	// Intentionally truncate the JSON so that we can emit "," after the last element.
	if (file)
		fclose(file);
}

#ifdef GRANITE_VULKAN_FILESYSTEM
TextureManager &Device::get_texture_manager()
{
	return texture_manager;
}

ShaderManager &Device::get_shader_manager()
{
	return shader_manager;
}
#endif

#ifdef GRANITE_VULKAN_FILESYSTEM
void Device::init_shader_manager_cache()
{
	//if (!shader_manager.load_shader_cache("assets://shader_cache.json"))
	//	shader_manager.load_shader_cache("cache://shader_cache.json");
	shader_manager.load_shader_cache("assets://shader_cache.json");
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

}
