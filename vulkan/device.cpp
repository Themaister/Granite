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

#include "device.hpp"
#include "format.hpp"
#include "thread_group.hpp"
#include "type_to_string.hpp"
#include "quirks.hpp"
#include "enum_cast.hpp"
#include <algorithm>
#include <string.h>

#ifdef VULKAN_MT
#define LOCK() std::lock_guard<std::mutex> holder__{lock.lock}
#define DRAIN_FRAME_LOCK() \
	std::unique_lock<std::mutex> holder__{lock.lock}; \
	lock.cond.wait(holder__, [&]() { \
		return lock.counter == 0; \
	})
#else
#define LOCK() ((void)0)
#define DRAIN_FRAME_LOCK() VK_ASSERT(lock.counter == 0)
#endif

using namespace std;
using namespace Util;

namespace Vulkan
{
Device::Device()
    : framebuffer_allocator(this)
    , transient_allocator(this)
	, physical_allocator(this)
	, shader_manager(this)
	, texture_manager(this)
{
	cookie.store(0);

	ThreadGroup::get_global();
	ThreadGroup::register_main_thread();
}

Semaphore Device::request_semaphore()
{
	LOCK();
	auto semaphore = managers.semaphore.request_cleared_semaphore();
	auto ptr = make_handle<SemaphoreHolder>(this, semaphore, false);
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
	auto ptr = make_handle<SemaphoreHolder>(this, semaphore, false);

	if (vkImportSemaphoreFdKHR(device, &import) != VK_SUCCESS)
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
}

void *Device::map_host_buffer(Buffer &buffer, MemoryAccessFlags access)
{
	void *host = managers.memory.map_memory(&buffer.get_allocation(), access);
	return host;
}

void Device::unmap_host_buffer(const Buffer &buffer)
{
	managers.memory.unmap_memory(buffer.get_allocation());
}

Shader *Device::request_shader(const uint32_t *data, size_t size)
{
	Util::Hasher hasher;
	hasher.data(data, size);

	auto hash = hasher.get();
	auto *ret = shaders.find(hash);
	if (!ret)
		ret = shaders.insert(hash, make_unique<Shader>(hash, this, data, size));
	return ret;
}

bool Device::enqueue_create_shader_module(Fossilize::Hash hash, unsigned, const VkShaderModuleCreateInfo *create_info, VkShaderModule *module)
{
	auto *ret = shaders.insert(hash, make_unique<Shader>(hash, this, create_info->pCode, create_info->codeSize));
	*module = ret->get_module();
	replayer_state.shader_map[*module] = ret;
	return true;
}

bool Device::enqueue_create_compute_pipeline(Fossilize::Hash hash, unsigned,
                                             const VkComputePipelineCreateInfo *create_info, VkPipeline *pipeline)
{
	// Find the Shader* associated with this VkShaderModule and just use that.
	auto itr = replayer_state.shader_map.find(create_info->stage.module);
	if (itr == end(replayer_state.shader_map))
		return false;

	auto *ret = request_program(itr->second);
	*pipeline = ret->get_compute_pipeline();
	return true;
}

Program *Device::request_program(Vulkan::Shader *compute)
{
	Util::Hasher hasher;
	hasher.u64(compute->get_hash());

	auto hash = hasher.get();
	auto *ret = programs.find(hash);
	if (!ret)
		ret = programs.insert(hash, make_unique<Program>(hash, this, compute));
	return ret;
}

Program *Device::request_program(const uint32_t *compute_data, size_t compute_size)
{
	auto *compute = request_shader(compute_data, compute_size);
	return request_program(compute);
}

bool Device::enqueue_create_graphics_pipeline(Fossilize::Hash hash, unsigned,
                                              const VkGraphicsPipelineCreateInfo *create_info, VkPipeline *pipeline)
{
	auto info = *create_info;

	if (info.stageCount != 2)
		return false;
	if (info.pStages[0].stage != VK_SHADER_STAGE_VERTEX_BIT)
		return false;
	if (info.pStages[1].stage != VK_SHADER_STAGE_FRAGMENT_BIT)
		return false;

	// Find the Shader* associated with this VkShaderModule and just use that.
	auto vertex_itr = replayer_state.shader_map.find(info.pStages[0].module);
	if (vertex_itr == end(replayer_state.shader_map))
		return false;

	// Find the Shader* associated with this VkShaderModule and just use that.
	auto fragment_itr = replayer_state.shader_map.find(info.pStages[1].module);
	if (fragment_itr == end(replayer_state.shader_map))
		return false;

	auto *ret = request_program(vertex_itr->second, fragment_itr->second);

	// The layout is dummy, resolve it here.
	info.layout = ret->get_pipeline_layout()->get_layout();

	get_state_recorder().register_graphics_pipeline(hash, info);
	LOGI("Creating graphics pipeline.\n");
	VkResult res = vkCreateGraphicsPipelines(device, pipeline_cache, 1, &info, nullptr, pipeline);
	if (res != VK_SUCCESS)
		LOGE("Failed to create graphics pipeline!\n");
	*pipeline = ret->add_graphics_pipeline(hash, *pipeline);
	return true;
}

Program *Device::request_program(Vulkan::Shader *vertex, Vulkan::Shader *fragment)
{
	Util::Hasher hasher;
	hasher.u64(vertex->get_hash());
	hasher.u64(fragment->get_hash());

	auto hash = hasher.get();
	auto *ret = programs.find(hash);

	if (!ret)
		ret = programs.insert(hash, make_unique<Program>(hash, this, vertex, fragment));
	return ret;
}

Program *Device::request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
                                 size_t fragment_size)
{
	auto *vertex = request_shader(vertex_data, vertex_size);
	auto *fragment = request_shader(fragment_data, fragment_size);
	return request_program(vertex, fragment);
}

PipelineLayout *Device::request_pipeline_layout(const CombinedResourceLayout &layout)
{
	Hasher h;
	h.data(reinterpret_cast<const uint32_t *>(layout.sets), sizeof(layout.sets));
	h.data(&layout.stages_for_bindings[0][0], sizeof(layout.stages_for_bindings));
	h.data(reinterpret_cast<const uint32_t *>(layout.ranges), sizeof(layout.ranges));
	h.u32(layout.attribute_mask);
	h.u32(layout.render_target_mask);

	auto hash = h.get();
	auto *ret = pipeline_layouts.find(hash);
	if (!ret)
		ret = pipeline_layouts.insert(hash, make_unique<PipelineLayout>(hash, this, layout));
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
		ret = descriptor_set_allocators.insert(hash, make_unique<DescriptorSetAllocator>(hash, this, layout, stages_for_bindings));
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
			});
		}

		layout.ranges[i].stageFlags = 1u << i;
		layout.ranges[i].offset = shader_layout.push_constant_offset;
		layout.ranges[i].size = shader_layout.push_constant_range;
	}

	for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
	{
		if (layout.stages_for_sets[i] != 0)
			layout.descriptor_set_mask |= 1u << i;
	}

	Hasher h;
	h.data(reinterpret_cast<uint32_t *>(layout.ranges), sizeof(layout.ranges));
	layout.push_constant_layout_hash = h.get();

	program.set_pipeline_layout(request_pipeline_layout(layout));

	if (program.get_shader(ShaderStage::Compute))
	{
		auto &shader = *program.get_shader(ShaderStage::Compute);
		VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		info.layout = program.get_pipeline_layout()->get_layout();
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.module = shader.get_module();
		info.stage.pName = "main";
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

#ifdef GRANITE_SPIRV_DUMP
		LOGI("Compiling SPIR-V file: (%s) %s\n",
		     Shader::stage_to_name(ShaderStage::Compute),
		     (to_string(shader.get_hash()) + ".spv").c_str());
#endif

		VkPipeline compute_pipeline;
		unsigned pipe_index = get_state_recorder().register_compute_pipeline(program.get_hash(), info);
		LOGI("Creating compute pipeline.\n");
		if (vkCreateComputePipelines(device, pipeline_cache, 1, &info, nullptr, &compute_pipeline) != VK_SUCCESS)
			LOGE("Failed to create compute pipeline!\n");
		get_state_recorder().set_compute_pipeline_handle(pipe_index, compute_pipeline);
		program.set_compute_pipeline(compute_pipeline);
	}
}

void Device::init_pipeline_cache()
{
	auto file = Filesystem::get().open("cache://pipeline_cache.bin", FileMode::ReadOnly);
	VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

	if (file)
	{
		auto size = file->get_size();
		static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
		if (size >= uuid_size)
		{
			uint8_t *mapped = static_cast<uint8_t *>(file->map());
			if (mapped)
			{
				if (memcmp(gpu_props.pipelineCacheUUID, mapped, uuid_size) == 0)
				{
					info.initialDataSize = size - uuid_size;
					info.pInitialData = mapped + uuid_size;
				}
			}
		}
	}

	vkCreatePipelineCache(device, &info, nullptr, &pipeline_cache);
}

void Device::init_pipeline_state()
{
	auto file = Filesystem::get().open("cache://pipelines.json", FileMode::ReadOnly);
	if (!file)
		return;

	void *mapped = file->map();
	if (!mapped)
	{
		LOGE("Failed to map pipelines.json.\n");
		return;
	}

	try
	{
		LOGI("Replaying cached state.\n");
		Fossilize::StateReplayer replayer;
		replayer.parse(*this, static_cast<const char *>(mapped), file->get_size());
		LOGI("Completed replaying cached state.\n");
		replayer_state = {};
	}
	catch (const exception &e)
	{
		LOGE("Exception caught while parsing pipeline state: %s.\n", e.what());
	}
}

void Device::flush_pipeline_state()
{
	auto serial = state_recorder.serialize();
	auto file = Filesystem::get().open("cache://pipelines.json", FileMode::WriteOnly);
	if (file)
	{
		uint8_t *data = static_cast<uint8_t *>(file->map_write(serial.size()));
		if (data)
		{
			memcpy(data, serial.data(), serial.size());
			file->unmap();
		}
		else
			LOGE("Failed to serialize pipeline data.\n");
	}
}

void Device::flush_pipeline_cache()
{
	static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
	size_t size = 0;
	if (vkGetPipelineCacheData(device, pipeline_cache, &size, nullptr) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}

	auto file = Filesystem::get().open("cache://pipeline_cache.bin", FileMode::WriteOnly);
	if (!file)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}

	uint8_t *data = static_cast<uint8_t *>(file->map_write(uuid_size + size));
	if (!data)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}

	memcpy(data, gpu_props.pipelineCacheUUID, uuid_size);
	if (vkGetPipelineCacheData(device, pipeline_cache, &size, data + uuid_size) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return;
	}
}

void Device::set_context(const Context &context)
{
	instance = context.get_instance();
	gpu = context.get_gpu();
	device = context.get_device();

	graphics_queue_family_index = context.get_graphics_queue_family();
	graphics_queue = context.get_graphics_queue();
	compute_queue_family_index = context.get_compute_queue_family();
	compute_queue = context.get_compute_queue();
	transfer_queue_family_index = context.get_transfer_queue_family();
	transfer_queue = context.get_transfer_queue();

	mem_props = context.get_mem_props();
	gpu_props = context.get_gpu_props();

	init_stock_samplers();
	init_pipeline_cache();
	init_pipeline_state();

	ext = context.get_enabled_device_features();

	managers.memory.init(gpu, device);
	managers.memory.set_supports_dedicated_allocation(ext.supports_dedicated);
	managers.semaphore.init(device);
	managers.fence.init(device);
	managers.event.init(device);
	managers.vbo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	managers.ibo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	managers.ubo.init(this, 256 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	managers.staging.init(this, 64 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.optimalBufferCopyOffsetAlignment), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
}

void Device::init_stock_samplers()
{
	SamplerCreateInfo info = {};
	info.maxLod = VK_LOD_CLAMP_NONE;
	info.maxAnisotropy = 1.0f;

	for (unsigned i = 0; i < static_cast<unsigned>(StockSampler::Count); i++)
	{
		auto mode = static_cast<StockSampler>(i);

		switch (mode)
		{
		case StockSampler::NearestShadow:
		case StockSampler::LinearShadow:
			info.compareEnable = true;
			info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			break;

		default:
			info.compareEnable = false;
			break;
		}

		switch (mode)
		{
		case StockSampler::TrilinearClamp:
		case StockSampler::TrilinearWrap:
			info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			break;

		default:
			info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		}

		switch (mode)
		{
		case StockSampler::LinearClamp:
		case StockSampler::LinearWrap:
		case StockSampler::TrilinearClamp:
		case StockSampler::TrilinearWrap:
		case StockSampler::LinearShadow:
			info.magFilter = VK_FILTER_LINEAR;
			info.minFilter = VK_FILTER_LINEAR;
			break;

		default:
			info.magFilter = VK_FILTER_NEAREST;
			info.minFilter = VK_FILTER_NEAREST;
			break;
		}

		switch (mode)
		{
		default:
		case StockSampler::LinearWrap:
		case StockSampler::NearestWrap:
		case StockSampler::TrilinearWrap:
			info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;

		case StockSampler::LinearClamp:
		case StockSampler::NearestClamp:
		case StockSampler::TrilinearClamp:
		case StockSampler::NearestShadow:
		case StockSampler::LinearShadow:
			info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		}
		samplers[i] = create_sampler(info);
		samplers[i]->set_internal_sync_object();
	}
}

static void request_block(Device &device, BufferBlock &block, VkDeviceSize size,
                          BufferPool &pool, std::vector<BufferBlock> *dma, std::vector<BufferBlock> &recycle)
{
	if (block.mapped)
		device.unmap_host_buffer(*block.cpu);

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

void Device::submit(CommandBufferHandle cmd, Fence *fence, Semaphore *semaphore, Semaphore *semaphore_alt)
{
	LOCK();
	submit_nolock(move(cmd), fence, semaphore, semaphore_alt);
}

void Device::submit_nolock(CommandBufferHandle cmd, Fence *fence, Semaphore *semaphore, Semaphore *semaphore_alt)
{
	auto type = cmd->get_command_buffer_type();
	auto &pool = get_command_pool(type, cmd->get_thread_index());
	auto &submissions = get_queue_submissions(type);

	pool.signal_submitted(cmd->get_command_buffer());
	cmd->end();
	submissions.push_back(move(cmd));

	VkFence cleared_fence = VK_NULL_HANDLE;

	if (fence || semaphore || semaphore_alt)
		submit_queue(type, fence ? &cleared_fence : nullptr, semaphore, semaphore_alt);

	if (fence)
	{
		VK_ASSERT(!*fence);
		*fence = make_handle<FenceHolder>(this, cleared_fence);
	}

	decrement_frame_counter_nolock();
}

void Device::submit_empty(CommandBuffer::Type type, Fence *fence, Semaphore *semaphore, Semaphore *semaphore_alt)
{
	LOCK();
	submit_empty_nolock(type, fence, semaphore, semaphore_alt);
}

void Device::submit_empty_nolock(CommandBuffer::Type type, Fence *fence, Semaphore *semaphore, Semaphore *semaphore_alt)
{
	if (type != CommandBuffer::Type::Transfer)
		flush_frame(CommandBuffer::Type::Transfer);

	VkFence cleared_fence = VK_NULL_HANDLE;
	submit_queue(type, fence ? &cleared_fence : nullptr, semaphore, semaphore_alt);
	if (fence)
		*fence = make_handle<FenceHolder>(this, cleared_fence);
}

void Device::submit_empty_inner(CommandBuffer::Type type, VkFence *fence, Semaphore *semaphore, Semaphore *semaphore_alt)
{
	auto &data = get_queue_data(type);
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

	// Add external wait semaphores.
	vector<VkSemaphore> waits;
	vector<VkSemaphore> signals;
	auto stages = move(data.wait_stages);

	for (auto &semaphore : data.wait_semaphores)
	{
		auto wait = semaphore->consume();
		if (semaphore->can_recycle())
			frame().recycled_semaphores.push_back(wait);
		else
			frame().destroyed_semaphores.push_back(wait);
		waits.push_back(wait);
	}
	data.wait_stages.clear();
	data.wait_semaphores.clear();

	// Add external signal semaphores.
	VkSemaphore cleared_semaphore = VK_NULL_HANDLE;
	VkSemaphore cleared_semaphore_alt = VK_NULL_HANDLE;
	if (semaphore)
	{
		cleared_semaphore = managers.semaphore.request_cleared_semaphore();
		signals.push_back(cleared_semaphore);
	}

	if (semaphore_alt)
	{
		cleared_semaphore_alt = managers.semaphore.request_cleared_semaphore();
		signals.push_back(cleared_semaphore_alt);
	}

	submit.signalSemaphoreCount = signals.size();
	submit.waitSemaphoreCount = waits.size();
	if (!signals.empty())
		submit.pSignalSemaphores = signals.data();
	if (!stages.empty())
		submit.pWaitDstStageMask = stages.data();
	if (!waits.empty())
		submit.pWaitSemaphores = waits.data();

	VkQueue queue;
	switch (type)
	{
	default:
	case CommandBuffer::Type::Graphics:
		queue = graphics_queue;
		break;
	case CommandBuffer::Type::Compute:
		queue = compute_queue;
		break;
	case CommandBuffer::Type::Transfer:
		queue = transfer_queue;
		break;
	}

	VkFence cleared_fence = fence ? managers.fence.request_cleared_fence() : VK_NULL_HANDLE;
	if (queue_lock_callback)
		queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
	if (cleared_fence)
		LOGI("Signalling Fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif
	VkResult result = vkQueueSubmit(queue, 1, &submit, cleared_fence);
	if (ImplementationQuirks::get().queue_wait_on_submission)
		vkQueueWaitIdle(queue);
	if (queue_unlock_callback)
		queue_unlock_callback();

	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", int(result));

	if (fence)
	{
		frame().wait_fences.push_back(cleared_fence);
		*fence = cleared_fence;
		data.need_fence = false;
	}
	else
		data.need_fence = true;

	if (semaphore)
	{
		VK_ASSERT(!*semaphore);
		*semaphore = make_handle<SemaphoreHolder>(this, cleared_semaphore, true);
	}

	if (semaphore_alt)
	{
		VK_ASSERT(!*semaphore_alt);
		*semaphore_alt = make_handle<SemaphoreHolder>(this, cleared_semaphore_alt, true);
	}

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
	const char *queue_name = nullptr;
	switch (type)
	{
	default:
	case CommandBuffer::Type::Graphics:
		queue_name = "Graphics";
		break;
	case CommandBuffer::Type::Compute:
		queue_name = "Compute";
		break;
	case CommandBuffer::Type::Transfer:
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

void Device::submit_staging(CommandBufferHandle cmd, VkBufferUsageFlags usage, bool flush)
{
	auto access = buffer_usage_to_possible_access(usage);
	auto stages = buffer_usage_to_possible_stages(usage);

	if (transfer_queue == graphics_queue && transfer_queue == compute_queue)
	{
		// For single-queue systems, just use a pipeline barrier.
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, stages, access);
		submit_nolock(cmd);
	}
	else
	{
		auto compute_stages = stages &
		                      (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
		                       VK_PIPELINE_STAGE_TRANSFER_BIT);

		auto compute_access = access &
		                      (VK_ACCESS_SHADER_READ_BIT |
		                       VK_ACCESS_SHADER_WRITE_BIT |
		                       VK_ACCESS_TRANSFER_READ_BIT |
		                       VK_ACCESS_UNIFORM_READ_BIT |
		                       VK_ACCESS_TRANSFER_WRITE_BIT);

		auto graphics_stages = stages;

		if (transfer_queue == graphics_queue)
		{
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             graphics_stages, access);

			if (compute_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::Compute, sem, compute_stages, flush);
			}
			else
				submit_nolock(cmd);
		}
		else if (transfer_queue == compute_queue)
		{
			cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			             compute_stages, compute_access);

			if (graphics_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::Graphics, sem, graphics_stages, flush);
			}
			else
				submit_nolock(cmd);
		}
		else
		{
			if (graphics_stages != 0 && compute_stages != 0)
			{
				Semaphore sem_graphics, sem_compute;
				submit_nolock(cmd, nullptr, &sem_graphics, &sem_compute);
				add_wait_semaphore_nolock(CommandBuffer::Type::Graphics, sem_graphics, graphics_stages, flush);
				add_wait_semaphore_nolock(CommandBuffer::Type::Compute, sem_compute, compute_stages, flush);
			}
			else if (graphics_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::Graphics, sem, graphics_stages, flush);
			}
			else if (compute_stages != 0)
			{
				Semaphore sem;
				submit_nolock(cmd, nullptr, &sem);
				add_wait_semaphore_nolock(CommandBuffer::Type::Compute, sem, compute_stages, flush);
			}
			else
				submit_nolock(cmd);
		}
	}
}

void Device::submit_queue(CommandBuffer::Type type, VkFence *fence, Semaphore *semaphore, Semaphore *semaphore_alt)
{
	// Always check if we need to flush pending transfers.
	if (type != CommandBuffer::Type::Transfer)
		flush_frame(CommandBuffer::Type::Transfer);

	auto &data = get_queue_data(type);
	auto &submissions = get_queue_submissions(type);

	if (submissions.empty())
	{
		if (fence || semaphore || semaphore_alt)
			submit_empty_inner(type, fence, semaphore, semaphore_alt);
		return;
	}

	vector<VkCommandBuffer> cmds;
	cmds.reserve(submissions.size());

	vector<VkSubmitInfo> submits;
	submits.reserve(2);
	size_t last_cmd = 0;

	vector<VkSemaphore> waits[2];
	vector<VkSemaphore> signals[2];
	vector<VkFlags> stages[2];

	// Add external wait semaphores.
	stages[0] = move(data.wait_stages);

	for (auto &semaphore : data.wait_semaphores)
	{
		auto wait = semaphore->consume();
		if (semaphore->can_recycle())
			frame().recycled_semaphores.push_back(wait);
		else
			frame().destroyed_semaphores.push_back(wait);
		waits[0].push_back(wait);
	}
	data.wait_stages.clear();
	data.wait_semaphores.clear();

	for (auto &cmd : submissions)
	{
		if (cmd->swapchain_touched() && !frame().swapchain_touched && !frame().swapchain_consumed)
		{
			if (!cmds.empty())
			{
				// Push all pending cmd buffers to their own submission.
				submits.emplace_back();

				auto &submit = submits.back();
				memset(&submit, 0, sizeof(submit));
				submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submit.pNext = nullptr;
				submit.commandBufferCount = cmds.size() - last_cmd;
				submit.pCommandBuffers = cmds.data() + last_cmd;
				last_cmd = cmds.size();
			}
			frame().swapchain_touched = true;
		}

		cmds.push_back(cmd->get_command_buffer());
	}

	if (cmds.size() > last_cmd)
	{
		unsigned index = submits.size();

		// Push all pending cmd buffers to their own submission.
		submits.emplace_back();

		auto &submit = submits.back();
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.pNext = nullptr;
		submit.commandBufferCount = cmds.size() - last_cmd;
		submit.pCommandBuffers = cmds.data() + last_cmd;
		if (frame().swapchain_touched && !frame().swapchain_consumed)
		{
			static const VkFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			if (wsi_acquire != VK_NULL_HANDLE)
			{
				waits[index].push_back(wsi_acquire);
				stages[index].push_back(wait);
			}

			VK_ASSERT(wsi_release != VK_NULL_HANDLE);
			signals[index].push_back(wsi_release);
			frame().swapchain_consumed = true;
		}
		last_cmd = cmds.size();
	}

	VkFence cleared_fence = fence ? managers.fence.request_cleared_fence() : VK_NULL_HANDLE;
	VkSemaphore cleared_semaphore = VK_NULL_HANDLE;
	VkSemaphore cleared_semaphore_alt = VK_NULL_HANDLE;
	if (semaphore)
	{
		cleared_semaphore = managers.semaphore.request_cleared_semaphore();
		signals[submits.size() - 1].push_back(cleared_semaphore);
	}

	if (semaphore_alt)
	{
		cleared_semaphore_alt = managers.semaphore.request_cleared_semaphore();
		signals[submits.size() - 1].push_back(cleared_semaphore_alt);
	}

	for (unsigned i = 0; i < submits.size(); i++)
	{
		auto &submit = submits[i];
		submit.waitSemaphoreCount = waits[i].size();
		if (!waits[i].empty())
		{
			submit.pWaitSemaphores = waits[i].data();
			submit.pWaitDstStageMask = stages[i].data();
		}

		submit.signalSemaphoreCount = signals[i].size();
		if (!signals[i].empty())
			submit.pSignalSemaphores = signals[i].data();
	}

	VkQueue queue;
	switch (type)
	{
	default:
	case CommandBuffer::Type::Graphics:
		queue = graphics_queue;
		break;
	case CommandBuffer::Type::Compute:
		queue = compute_queue;
		break;
	case CommandBuffer::Type::Transfer:
		queue = transfer_queue;
		break;
	}

	if (queue_lock_callback)
		queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
	if (cleared_fence)
		LOGI("Signalling fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif
	VkResult result = vkQueueSubmit(queue, submits.size(), submits.data(), cleared_fence);
	if (ImplementationQuirks::get().queue_wait_on_submission)
		vkQueueWaitIdle(queue);
	if (queue_unlock_callback)
		queue_unlock_callback();
	if (result != VK_SUCCESS)
		LOGE("vkQueueSubmit failed (code: %d).\n", int(result));
	submissions.clear();

	if (fence)
	{
		frame().wait_fences.push_back(cleared_fence);
		*fence = cleared_fence;
		data.need_fence = false;
	}
	else
		data.need_fence = true;

	if (semaphore)
	{
		VK_ASSERT(!*semaphore);
		*semaphore = make_handle<SemaphoreHolder>(this, cleared_semaphore, true);
	}

	if (semaphore_alt)
	{
		VK_ASSERT(!*semaphore_alt);
		*semaphore_alt = make_handle<SemaphoreHolder>(this, cleared_semaphore_alt, true);
	}

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
	const char *queue_name = nullptr;
	switch (type)
	{
	default:
	case CommandBuffer::Type::Graphics:
		queue_name = "Graphics";
		break;
	case CommandBuffer::Type::Compute:
		queue_name = "Compute";
		break;
	case CommandBuffer::Type::Transfer:
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
	if (type == CommandBuffer::Type::Transfer)
		sync_buffer_blocks();
	submit_queue(type, nullptr, nullptr, nullptr);
}

void Device::sync_buffer_blocks()
{
	if (dma.vbo.empty() && dma.ibo.empty() && dma.ubo.empty())
		return;

	VkBufferUsageFlags usage = 0;
	auto cmd = request_command_buffer_nolock(ThreadGroup::get_current_thread_index(), CommandBuffer::Type::Transfer);

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

void Device::end_frame()
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
	VkFence fence;

	if (transfer.need_fence || !frame().transfer_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::Transfer, &fence, nullptr, nullptr);
		frame().recycle_fences.push_back(fence);
		transfer.need_fence = false;
	}

	if (graphics.need_fence || !frame().graphics_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::Graphics, &fence, nullptr, nullptr);
		frame().recycle_fences.push_back(fence);
		graphics.need_fence = false;
	}

	if (compute.need_fence || !frame().compute_submissions.empty())
	{
		submit_queue(CommandBuffer::Type::Compute, &fence, nullptr, nullptr);
		frame().recycle_fences.push_back(fence);
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
	flush_frame(CommandBuffer::Type::Transfer);
	flush_frame(CommandBuffer::Type::Graphics);
	flush_frame(CommandBuffer::Type::Compute);
}

Device::QueueData &Device::get_queue_data(CommandBuffer::Type type)
{
	switch (type)
	{
	default:
	case CommandBuffer::Type::Graphics:
		return graphics;
	case CommandBuffer::Type::Compute:
		return compute;
	case CommandBuffer::Type::Transfer:
		return transfer;
	}
}

CommandPool &Device::get_command_pool(CommandBuffer::Type type, unsigned thread)
{
	switch (type)
	{
	default:
	case CommandBuffer::Type::Graphics:
		return frame().graphics_cmd_pool[thread];
	case CommandBuffer::Type::Compute:
		return frame().compute_cmd_pool[thread];
	case CommandBuffer::Type::Transfer:
		return frame().transfer_cmd_pool[thread];
	}
}

vector<CommandBufferHandle> &Device::get_queue_submissions(CommandBuffer::Type type)
{
	switch (type)
	{
	default:
	case CommandBuffer::Type::Graphics:
		return frame().graphics_submissions;
	case CommandBuffer::Type::Compute:
		return frame().compute_submissions;
	case CommandBuffer::Type::Transfer:
		return frame().transfer_submissions;
	}
}

CommandBufferHandle Device::request_command_buffer(CommandBuffer::Type type)
{
	return request_command_buffer_for_thread(ThreadGroup::get_current_thread_index(), type);
}

CommandBufferHandle Device::request_command_buffer_for_thread(unsigned thread_index, CommandBuffer::Type type)
{
	LOCK();
	return request_command_buffer_nolock(thread_index, type);
}

CommandBufferHandle Device::request_command_buffer_nolock(unsigned thread_index, CommandBuffer::Type type)
{
	auto cmd = get_command_pool(type, thread_index).request_command_buffer();

	VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &info);
	add_frame_counter_nolock();
	auto handle = make_handle<CommandBuffer>(this, cmd, pipeline_cache, type);
	handle->set_thread_index(thread_index);
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
	vkCmdExecuteCommands(primary.get_command_buffer(), 1, &secondary_cmd);
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

	inherit.framebuffer = framebuffer->get_framebuffer();
	inherit.renderPass = framebuffer->get_render_pass().get_render_pass();
	inherit.subpass = subpass;
	info.pInheritanceInfo = &inherit;
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

	vkBeginCommandBuffer(cmd, &info);
	add_frame_counter_nolock();
	auto handle = make_handle<CommandBuffer>(this, cmd, pipeline_cache, type);
	handle->set_thread_index(thread_index);
	handle->set_is_secondary();
	return handle;
}

VkSemaphore Device::set_acquire(VkSemaphore acquire)
{
	swap(acquire, wsi_acquire);
	return acquire;
}

VkSemaphore Device::set_release(VkSemaphore release)
{
	swap(release, wsi_release);
	return release;
}

const Sampler &Device::get_stock_sampler(StockSampler sampler) const
{
	return *samplers[static_cast<unsigned>(sampler)];
}

bool Device::swapchain_touched() const
{
	return frame().swapchain_touched;
}

Device::~Device()
{
	ThreadGroup::get_global().wait_idle();
	wait_idle();

	if (wsi_acquire != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(device, wsi_acquire, nullptr);
		wsi_acquire = VK_NULL_HANDLE;
	}

	if (wsi_release != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(device, wsi_release, nullptr);
		wsi_release = VK_NULL_HANDLE;
	}

	if (pipeline_cache != VK_NULL_HANDLE)
	{
		flush_pipeline_cache();
		vkDestroyPipelineCache(device, pipeline_cache, nullptr);
	}

	flush_pipeline_state();

	framebuffer_allocator.clear();
	transient_allocator.clear();
	for (auto &sampler : samplers)
		sampler.reset();

	for (auto &frame : per_frame)
		frame->release_owned_resources();
}

void Device::init_external_swapchain(const vector<ImageHandle> &swapchain_images)
{
	DRAIN_FRAME_LOCK();
	wait_idle_nolock();

	// Clear out caches which might contain stale data from now on.
	framebuffer_allocator.clear();
	transient_allocator.clear();

	for (auto &frame : per_frame)
		frame->release_owned_resources();
	per_frame.clear();

	for (auto &image : swapchain_images)
	{
		auto frame = make_unique<PerFrame>(this, managers,
		                                   graphics_queue_family_index,
		                                   compute_queue_family_index,
		                                   transfer_queue_family_index);

		frame->backbuffer = image;
		per_frame.emplace_back(move(frame));
	}
}

void Device::init_swapchain(const vector<VkImage> &swapchain_images, unsigned width, unsigned height, VkFormat format)
{
	DRAIN_FRAME_LOCK();
	wait_idle_nolock();

	// Clear out caches which might contain stale data from now on.
	framebuffer_allocator.clear();
	transient_allocator.clear();

	for (auto &frame : per_frame)
		frame->release_owned_resources();
	per_frame.clear();

	const auto info = ImageCreateInfo::render_target(width, height, format);

	for (auto &image : swapchain_images)
	{
		auto frame = make_unique<PerFrame>(this, managers,
		                                   graphics_queue_family_index,
		                                   compute_queue_family_index,
		                                   transfer_queue_family_index);

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
		if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
			LOGE("Failed to create view for backbuffer.");

		frame->backbuffer = make_handle<Image>(this, image, image_view, DeviceAllocation{}, info);
		set_name(*frame->backbuffer, "backbuffer");
		frame->backbuffer->set_swapchain_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		per_frame.emplace_back(move(frame));
	}
}

Device::PerFrame::PerFrame(Device *device, Managers &managers,
                           uint32_t graphics_queue_family_index,
                           uint32_t compute_queue_family_index,
                           uint32_t transfer_queue_family_index)
    : device(device->get_device())
    , managers(managers)
    , query_pool(device)
{
	unsigned count = ThreadGroup::get_global().get_num_threads() + 1;
	for (unsigned i = 0; i < count; i++)
	{
		graphics_cmd_pool.emplace_back(device->get_device(), graphics_queue_family_index);
		compute_cmd_pool.emplace_back(device->get_device(), compute_queue_family_index);
		transfer_cmd_pool.emplace_back(device->get_device(), transfer_queue_family_index);
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

void Device::reset_fence(VkFence fence)
{
	LOCK();
	frame().recycle_fences.push_back(fence);
}

void Device::destroy_buffer(VkBuffer buffer)
{
	LOCK();
	destroy_buffer_nolock(buffer);
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

void Device::destroy_event_nolock(VkEvent event)
{
	VK_ASSERT(!exists(frame().recycled_events, event));
	frame().recycled_events.push_back(event);
}

PipelineEvent Device::request_pipeline_event()
{
	return Util::make_handle<EventHolder>(this, managers.event.request_cleared_event());
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
		vkDestroySemaphore(device, sem->consume(), nullptr);
	for (auto &sem : compute.wait_semaphores)
		vkDestroySemaphore(device, sem->consume(), nullptr);
	for (auto &sem : transfer.wait_semaphores)
		vkDestroySemaphore(device, sem->consume(), nullptr);

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
		vkDeviceWaitIdle(device);
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
	for (auto &allocator : descriptor_set_allocators.get_hashmap())
		allocator.second->clear();

	for (auto &frame : per_frame)
	{
		// Avoid double-wait-on-semaphore scenarios.
		bool touched_swapchain = frame->swapchain_touched;

		// We have done WaitIdle, no need to wait for extra fences, it's also not safe.
		frame->wait_fences.clear();

		frame->begin();
		frame->swapchain_touched = touched_swapchain;
	}
}

void Device::begin_frame(unsigned index)
{
	DRAIN_FRAME_LOCK();

	// Flush the frame here as we might have pending staging command buffers from init stage.
	end_frame_nolock();

	framebuffer_allocator.begin_frame();
	transient_allocator.begin_frame();
	physical_allocator.begin_frame();
	for (auto &allocator : descriptor_set_allocators.get_hashmap())
		allocator.second->begin_frame();

	current_swapchain_index = index;
	frame().begin();
}

QueryPoolHandle Device::write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage)
{
	LOCK();
	return frame().query_pool.write_timestamp(cmd, stage);
}

void Device::add_frame_counter()
{
	LOCK();
	add_frame_counter_nolock();
}

void Device::decrement_frame_counter()
{
	LOCK();
	decrement_frame_counter_nolock();
}

void Device::add_frame_counter_nolock()
{
	lock.counter++;
}

void Device::decrement_frame_counter_nolock()
{
	VK_ASSERT(lock.counter > 0);
	lock.counter--;
#ifdef VULKAN_MT
	lock.cond.notify_one();
#endif
}

void Device::PerFrame::begin()
{
	if (!wait_fences.empty())
	{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		for (auto &fence : wait_fences)
			LOGI("Waiting for Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
		vkWaitForFences(device, wait_fences.size(), wait_fences.data(), VK_TRUE, UINT64_MAX);
		wait_fences.clear();
	}

	if (!recycle_fences.empty())
	{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		for (auto &fence : recycle_fences)
			LOGI("Recycling Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
		vkResetFences(device, recycle_fences.size(), recycle_fences.data());
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

	for (auto &framebuffer : destroyed_framebuffers)
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	for (auto &sampler : destroyed_samplers)
		vkDestroySampler(device, sampler, nullptr);
	for (auto &pipeline : destroyed_pipelines)
		vkDestroyPipeline(device, pipeline, nullptr);
	for (auto &view : destroyed_image_views)
		vkDestroyImageView(device, view, nullptr);
	for (auto &view : destroyed_buffer_views)
		vkDestroyBufferView(device, view, nullptr);
	for (auto &image : destroyed_images)
		vkDestroyImage(device, image, nullptr);
	for (auto &buffer : destroyed_buffers)
		vkDestroyBuffer(device, buffer, nullptr);
	for (auto &semaphore : destroyed_semaphores)
		vkDestroySemaphore(device, semaphore, nullptr);
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
	recycled_semaphores.clear();
	recycled_events.clear();
	allocations.clear();

	swapchain_touched = false;
	swapchain_consumed = false;
}

void Device::PerFrame::release_owned_resources()
{
	if (backbuffer)
	{
		backbuffer->set_internal_sync_object();
		backbuffer->get_view().set_internal_sync_object();
	}
	backbuffer.reset();
}

Device::PerFrame::~PerFrame()
{
	begin();
}

uint32_t Device::find_memory_type(BufferDomain domain, uint32_t mask)
{
	uint32_t desired = 0, fallback = 0;
	switch (domain)
	{
	case BufferDomain::Device:
		desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = 0;
		break;

	case BufferDomain::LinkedDeviceHost:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;

	case BufferDomain::Host:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;

	case BufferDomain::CachedHost:
		desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
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

	throw runtime_error("Couldn't find memory type.");
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

	throw runtime_error("Couldn't find memory type.");
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
	auto res = vkCreateBufferView(device, &info, nullptr, &view);
	if (res != VK_SUCCESS)
		return BufferViewHandle(nullptr);

	return make_handle<BufferView>(this, view, view_info);
}

ImageViewHandle Device::create_image_view(const ImageViewCreateInfo &create_info)
{
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
	view_info.viewType = get_image_view_type(image_create_info, &create_info);

	unsigned num_levels;
	if (view_info.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS)
		num_levels = create_info.image->get_create_info().levels - view_info.subresourceRange.baseMipLevel;
	else
		num_levels = view_info.subresourceRange.levelCount;

	VkImageView image_view = VK_NULL_HANDLE;
	VkImageView depth_view = VK_NULL_HANDLE;
	VkImageView stencil_view = VK_NULL_HANDLE;
	VkImageView base_level_view = VK_NULL_HANDLE;
	if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
		return ImageViewHandle(nullptr);

	if (num_levels > 1)
	{
		view_info.subresourceRange.levelCount = 1;
		if (vkCreateImageView(device, &view_info, nullptr, &base_level_view) != VK_SUCCESS)
		{
			vkDestroyImageView(device, image_view, nullptr);
			return ImageViewHandle(nullptr);
		}
		view_info.subresourceRange.levelCount = create_info.levels;
	}

	// If the image has multiple aspects, make split up images.
	if (view_info.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
	{
		view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (vkCreateImageView(device, &view_info, nullptr, &depth_view) != VK_SUCCESS)
		{
			vkDestroyImageView(device, image_view, nullptr);
			vkDestroyImageView(device, base_level_view, nullptr);
			return ImageViewHandle(nullptr);
		}

		view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		if (vkCreateImageView(device, &view_info, nullptr, &stencil_view) != VK_SUCCESS)
		{
			vkDestroyImageView(device, image_view, nullptr);
			vkDestroyImageView(device, depth_view, nullptr);
			vkDestroyImageView(device, base_level_view, nullptr);
			return ImageViewHandle(nullptr);
		}
	}

	ImageViewCreateInfo tmp = create_info;
	tmp.format = format;
	auto ret = make_handle<ImageView>(this, image_view, tmp);
	ret->set_alt_views(depth_view, stencil_view);
	ret->set_base_level_view(base_level_view);
	return ret;
}

#ifndef _WIN32
ImageHandle Device::create_imported_image(int fd, VkDeviceSize size, uint32_t memory_type,
                                          VkExternalMemoryHandleTypeFlagBitsKHR handle_type,
                                          const ImageCreateInfo &create_info)
{
	if (!ext.supports_external)
		return {};

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

	VK_ASSERT(image_format_is_supported(create_info.format, image_usage_to_features(info.usage)));

	VkImage image;
	if (vkCreateImage(device, &info, nullptr, &image) != VK_SUCCESS)
		return ImageHandle(nullptr);

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = size;
	alloc_info.memoryTypeIndex = memory_type;

	VkMemoryDedicatedAllocateInfoKHR dedicated_info = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR };
	dedicated_info.image = image;
	alloc_info.pNext = &dedicated_info;

	VkImportMemoryFdInfoKHR fd_info = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
	fd_info.handleType = handle_type;
	fd_info.fd = fd;
	dedicated_info.pNext = &fd_info;

	VkDeviceMemory memory;

	VkMemoryRequirements reqs;
	vkGetImageMemoryRequirements(device, image, &reqs);
	if (reqs.size > size)
	{
		vkDestroyImage(device, image, nullptr);
		return ImageHandle(nullptr);
	}

	if (((1u << memory_type) & reqs.memoryTypeBits) == 0)
	{
		vkDestroyImage(device, image, nullptr);
		return ImageHandle(nullptr);
	}

	if (vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS)
	{
		vkDestroyImage(device, image, nullptr);
		return ImageHandle(nullptr);
	}

	if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS)
	{
		vkDestroyImage(device, image, nullptr);
		vkFreeMemory(device, memory, nullptr);
		return ImageHandle(nullptr);
	}

	// Create a default image view.
	VkImageView image_view = VK_NULL_HANDLE;
	VkImageView depth_view = VK_NULL_HANDLE;
	VkImageView stencil_view = VK_NULL_HANDLE;
	VkImageView base_level_view = VK_NULL_HANDLE;
	if (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
	{
		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.format = create_info.format;
		view_info.components = create_info.swizzle;
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(view_info.format);
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.levelCount = info.mipLevels;
		view_info.subresourceRange.layerCount = info.arrayLayers;
		view_info.viewType = get_image_view_type(create_info, nullptr);

		if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
		{
			vkFreeMemory(device, memory, nullptr);
			vkDestroyImage(device, image, nullptr);
			return ImageHandle(nullptr);
		}

		if (info.mipLevels > 1)
		{
			view_info.subresourceRange.levelCount = 1;
			if (vkCreateImageView(device, &view_info, nullptr, &base_level_view) != VK_SUCCESS)
			{
				vkFreeMemory(device, memory, nullptr);
				vkDestroyImage(device, image, nullptr);
				vkDestroyImageView(device, image_view, nullptr);
				return ImageHandle(nullptr);
			}
			view_info.subresourceRange.levelCount = info.mipLevels;
		}

		// If the image has multiple aspects, make split up images.
		if (view_info.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
		{
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (vkCreateImageView(device, &view_info, nullptr, &depth_view) != VK_SUCCESS)
			{
				vkFreeMemory(device, memory, nullptr);
				vkDestroyImageView(device, image_view, nullptr);
				vkDestroyImageView(device, base_level_view, nullptr);
				vkDestroyImage(device, image, nullptr);
				return ImageHandle(nullptr);
			}

			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
			if (vkCreateImageView(device, &view_info, nullptr, &stencil_view) != VK_SUCCESS)
			{
				vkFreeMemory(device, memory, nullptr);
				vkDestroyImageView(device, image_view, nullptr);
				vkDestroyImageView(device, depth_view, nullptr);
				vkDestroyImageView(device, base_level_view, nullptr);
				vkDestroyImage(device, image, nullptr);
				return ImageHandle(nullptr);
			}
		}
	}

	auto allocation = DeviceAllocation::make_imported_allocation(memory, size, memory_type);
	auto handle = make_handle<Image>(this, image, image_view, allocation, create_info);
	handle->get_view().set_alt_views(depth_view, stencil_view);
	handle->get_view().set_base_level_view(base_level_view);

	// Set possible dstStage and dstAccess.
	handle->set_stage_flags(image_usage_to_possible_stages(info.usage));
	handle->set_access_flags(image_usage_to_possible_access(info.usage));
	return handle;
}
#endif

InitialImageBuffer Device::create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial)
{
	InitialImageBuffer result;

	unsigned mip_levels = info.levels;
	if (mip_levels == 0)
		mip_levels = image_num_miplevels({ info.width, info.height, info.depth });

	auto width = info.width;
	auto height = info.height;
	auto depth = info.depth;

	bool generate_mips = (info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;
	unsigned copy_levels = generate_mips ? 1u : mip_levels;
	unsigned index = 0;

	// Figure out how much memory we need.
	VkDeviceSize required_size = 0;

	for (unsigned level = 0; level < copy_levels; level++)
	{
		for (unsigned layer = 0; layer < info.layers; layer++, index++)
		{
			uint32_t row_length = initial[index].row_length ? initial[index].row_length : width;
			uint32_t array_height = initial[index].array_height ? initial[index].array_height : height;

			uint32_t blocks_x = row_length;
			uint32_t blocks_y = array_height;
			format_num_blocks(info.format, blocks_x, blocks_y);
			format_align_dim(info.format, row_length, array_height);

			VkDeviceSize size =
					format_block_size(info.format) * depth * blocks_x * blocks_y;

			// Align to cache line for good measure.
			required_size = (required_size + 63) & ~63;

			VkBufferImageCopy copy = {};
			copy.bufferRowLength = initial[index].row_length != width ? initial[index].row_length : 0;
			copy.bufferImageHeight = initial[index].array_height != height ? initial[index].array_height : 0;
			copy.imageExtent.width = width;
			copy.imageExtent.height = height;
			copy.imageExtent.depth = depth;
			copy.imageSubresource.aspectMask = format_to_aspect_mask(info.format);
			copy.imageSubresource.layerCount = 1;
			copy.imageSubresource.baseArrayLayer = layer;
			copy.imageSubresource.mipLevel = level;
			copy.bufferOffset = required_size;
			result.blits.push_back(copy);

			required_size += size;
		}

		width = max(width >> 1u, 1u);
		height = max(height >> 1u, 1u);
		depth = max(depth >> 1u, 1u);
	}

	BufferCreateInfo buffer_info = {};
	buffer_info.domain = BufferDomain::Host;
	buffer_info.size = required_size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	result.buffer = create_buffer(buffer_info, nullptr);
	set_name(*result.buffer, "image-upload-staging-buffer");

	// And now, do the actual copy.
	auto *mapped = static_cast<uint8_t *>(map_host_buffer(*result.buffer, MEMORY_ACCESS_WRITE));
	required_size = 0;
	width = info.width;
	height = info.height;
	depth = info.depth;
	index = 0;

	for (unsigned level = 0; level < copy_levels; level++)
	{
		for (unsigned layer = 0; layer < info.layers; layer++, index++)
		{
			uint32_t row_length = initial[index].row_length ? initial[index].row_length : width;
			uint32_t array_height = initial[index].array_height ? initial[index].array_height : height;

			uint32_t blocks_x = row_length;
			uint32_t blocks_y = array_height;
			format_num_blocks(info.format, blocks_x, blocks_y);
			format_align_dim(info.format, row_length, array_height);

			VkDeviceSize size =
					format_block_size(info.format) * depth * blocks_x * blocks_y;

			// Align to cache line for good measure.
			required_size = (required_size + 63) & ~63;
			memcpy(mapped + required_size, initial[index].data, size);
			required_size += size;
		}

		width = max(width >> 1u, 1u);
		height = max(height >> 1u, 1u);
		depth = max(depth >> 1u, 1u);
	}

	unmap_host_buffer(*result.buffer);
	return result;
}

static bool fill_image_format_list(VkFormat *formats, VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		formats[0] = VK_FORMAT_R8G8B8A8_UNORM;
		formats[1] = VK_FORMAT_R8G8B8A8_SRGB;
		return true;

	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
		formats[1] = VK_FORMAT_B8G8R8A8_SRGB;
		return true;

	case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
	case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
		formats[0] = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
		formats[1] = VK_FORMAT_A8B8G8R8_SRGB_PACK32;
		return true;

	default:
		return false;
	}
}

ImageHandle Device::create_image(const ImageCreateInfo &create_info, const ImageInitialData *initial)
{
	VkImage image;
	VkMemoryRequirements reqs;
	DeviceAllocation allocation;

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
	if (create_info.domain == ImageDomain::Transient)
		info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	if (initial)
		info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	info.flags = create_info.flags;

	VkImageFormatListCreateInfoKHR format_info = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR };
	VkFormat view_formats[2];
	format_info.pViewFormats = view_formats;
	format_info.viewFormatCount = 2;
	bool create_unorm_srgb_views = false;

	if (create_info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT)
	{
		if (fill_image_format_list(view_formats, info.format))
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

	if (info.mipLevels == 0)
		info.mipLevels = image_num_miplevels(info.extent);

	// Only do this conditionally.
	// On AMD, using CONCURRENT with async compute disables compression.
	uint32_t sharing_indices[3];
	bool concurrent_queue = (create_info.misc & IMAGE_MISC_CONCURRENT_QUEUE_BIT) != 0;
	if (concurrent_queue &&
	    (graphics_queue_family_index != compute_queue_family_index ||
	     graphics_queue_family_index != transfer_queue_family_index))
	{
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

	VK_ASSERT(image_format_is_supported(create_info.format, image_usage_to_features(info.usage)));

	if (vkCreateImage(device, &info, nullptr, &image) != VK_SUCCESS)
		return ImageHandle(nullptr);

	vkGetImageMemoryRequirements(device, image, &reqs);
	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
	if (!managers.memory.allocate_image_memory(reqs.size, reqs.alignment, memory_type, ALLOCATION_TILING_OPTIMAL,
	                                           &allocation, image))
	{
		vkDestroyImage(device, image, nullptr);
		return ImageHandle(nullptr);
	}

	if (vkBindImageMemory(device, image, allocation.get_memory(), allocation.get_offset()) != VK_SUCCESS)
	{
		allocation.free_immediate(managers.memory);
		vkDestroyImage(device, image, nullptr);
		return ImageHandle(nullptr);
	}

	auto tmpinfo = create_info;
	tmpinfo.usage = info.usage;
	tmpinfo.levels = info.mipLevels;

	// Create a default image view.
	VkImageView image_view = VK_NULL_HANDLE;
	VkImageView depth_view = VK_NULL_HANDLE;
	VkImageView stencil_view = VK_NULL_HANDLE;
	VkImageView base_level_view = VK_NULL_HANDLE;
	VkImageView unorm_view = VK_NULL_HANDLE;
	VkImageView srgb_view = VK_NULL_HANDLE;
	if (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
	{
		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.format = create_info.format;
		view_info.components = create_info.swizzle;
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(view_info.format);
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.levelCount = info.mipLevels;
		view_info.subresourceRange.layerCount = info.arrayLayers;
		view_info.viewType = get_image_view_type(tmpinfo, nullptr);

		if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
		{
			allocation.free_immediate(managers.memory);
			vkDestroyImage(device, image, nullptr);
			return ImageHandle(nullptr);
		}

		if (create_unorm_srgb_views)
		{
			view_info.format = view_formats[0];
			if (vkCreateImageView(device, &view_info, nullptr, &unorm_view) != VK_SUCCESS)
			{
				allocation.free_immediate(managers.memory);
				vkDestroyImageView(device, image_view, nullptr);
				vkDestroyImage(device, image, nullptr);
				return ImageHandle(nullptr);
			}

			view_info.format = view_formats[1];
			if (vkCreateImageView(device, &view_info, nullptr, &srgb_view) != VK_SUCCESS)
			{
				allocation.free_immediate(managers.memory);
				vkDestroyImageView(device, image_view, nullptr);
				if (unorm_view != VK_NULL_HANDLE)
					vkDestroyImageView(device, unorm_view, nullptr);
				vkDestroyImage(device, image, nullptr);
				return ImageHandle(nullptr);
			}

			view_info.format = create_info.format;
		}

		if (info.mipLevels > 1)
		{
			view_info.subresourceRange.levelCount = 1;
			if (vkCreateImageView(device, &view_info, nullptr, &base_level_view) != VK_SUCCESS)
			{
				allocation.free_immediate(managers.memory);
				vkDestroyImage(device, image, nullptr);
				vkDestroyImageView(device, image_view, nullptr);
				if (unorm_view != VK_NULL_HANDLE)
					vkDestroyImageView(device, unorm_view, nullptr);
				if (srgb_view != VK_NULL_HANDLE)
					vkDestroyImageView(device, srgb_view, nullptr);
				return ImageHandle(nullptr);
			}
			view_info.subresourceRange.levelCount = info.mipLevels;
		}

		// If the image has multiple aspects, make split up images.
		if (view_info.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
		{
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (vkCreateImageView(device, &view_info, nullptr, &depth_view) != VK_SUCCESS)
			{
				allocation.free_immediate(managers.memory);
				vkDestroyImageView(device, image_view, nullptr);
				vkDestroyImageView(device, base_level_view, nullptr);
				if (unorm_view != VK_NULL_HANDLE)
					vkDestroyImageView(device, unorm_view, nullptr);
				if (srgb_view != VK_NULL_HANDLE)
					vkDestroyImageView(device, srgb_view, nullptr);
				vkDestroyImage(device, image, nullptr);
				return ImageHandle(nullptr);
			}

			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
			if (vkCreateImageView(device, &view_info, nullptr, &stencil_view) != VK_SUCCESS)
			{
				allocation.free_immediate(managers.memory);
				vkDestroyImageView(device, image_view, nullptr);
				vkDestroyImageView(device, depth_view, nullptr);
				vkDestroyImageView(device, base_level_view, nullptr);
				if (unorm_view != VK_NULL_HANDLE)
					vkDestroyImageView(device, unorm_view, nullptr);
				if (srgb_view != VK_NULL_HANDLE)
					vkDestroyImageView(device, srgb_view, nullptr);
				vkDestroyImage(device, image, nullptr);
				return ImageHandle(nullptr);
			}
		}
	}

	auto handle = make_handle<Image>(this, image, image_view, allocation, tmpinfo);
	handle->get_view().set_alt_views(depth_view, stencil_view);
	handle->get_view().set_base_level_view(base_level_view);
	handle->get_view().set_unorm_view(unorm_view);
	handle->get_view().set_srgb_view(srgb_view);

	// Set possible dstStage and dstAccess.
	handle->set_stage_flags(image_usage_to_possible_stages(info.usage));
	handle->set_access_flags(image_usage_to_possible_access(info.usage));

	// Copy initial data to texture.
	if (initial)
	{
		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);
		auto staging_buffer = create_image_staging_buffer(create_info, initial);
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

		auto transfer_cmd = request_command_buffer(CommandBuffer::Type::Transfer);
		auto graphics_cmd = request_command_buffer(CommandBuffer::Type::Graphics);

		transfer_cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                            VK_ACCESS_TRANSFER_WRITE_BIT);

		handle->set_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		transfer_cmd->begin_region("copy-image-to-gpu");
		transfer_cmd->copy_buffer_to_image(*handle, *staging_buffer.buffer, staging_buffer.blits.size(), staging_buffer.blits.data());
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
				release.oldLayout = handle->get_layout();

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

				handle->set_layout(release.newLayout);
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
			submit(transfer_cmd, nullptr, &sem);
			add_wait_semaphore(CommandBuffer::Type::Graphics, sem, dst_stages, true);
		}
		else
			submit(transfer_cmd);

		if (generate_mips)
		{
			graphics_cmd->begin_region("mipgen");
			graphics_cmd->barrier_prepare_generate_mipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                                              VK_PIPELINE_STAGE_TRANSFER_BIT,
			                                              prepare_src_access, need_mipmap_barrier);
			handle->set_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			graphics_cmd->generate_mipmap(*handle);
			graphics_cmd->end_region();
		}

		if (need_initial_barrier)
		{
			graphics_cmd->image_barrier(
					*handle, handle->get_layout(), create_info.initial_layout,
					VK_PIPELINE_STAGE_TRANSFER_BIT, final_transition_src_access,
					handle->get_stage_flags(),
					handle->get_access_flags() & image_layout_to_possible_access(create_info.initial_layout));
		}

		// For concurrent queue, make sure that compute can see the final image as well.
		if (concurrent_queue && graphics_queue != compute_queue)
		{
			Semaphore sem;
			submit(graphics_cmd, nullptr, &sem);
			add_wait_semaphore(CommandBuffer::Type::Compute,
			                   sem, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
		}
		else
			submit(graphics_cmd);
	}
	else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
	{
		VK_ASSERT(create_info.domain != ImageDomain::Transient);
		auto cmd = request_command_buffer(CommandBuffer::Type::Graphics);
		cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, create_info.initial_layout,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, handle->get_stage_flags(),
		                   handle->get_access_flags() &
		                   image_layout_to_possible_access(create_info.initial_layout));

		// For concurrent queue, make sure that compute can see the final image as well.
		if (concurrent_queue && graphics_queue != compute_queue)
		{
			Semaphore sem;
			submit(cmd, nullptr, &sem);
			add_wait_semaphore(CommandBuffer::Type::Compute,
			                   sem, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
		}
		else
			submit(cmd);
	}

	handle->set_layout(create_info.initial_layout);
	return handle;
}

SamplerHandle Device::create_sampler(const SamplerCreateInfo &sampler_info)
{
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	info.magFilter = sampler_info.magFilter;
	info.minFilter = sampler_info.minFilter;
	info.mipmapMode = sampler_info.mipmapMode;
	info.addressModeU = sampler_info.addressModeU;
	info.addressModeV = sampler_info.addressModeV;
	info.addressModeW = sampler_info.addressModeW;
	info.mipLodBias = sampler_info.mipLodBias;
	info.anisotropyEnable = sampler_info.anisotropyEnable;
	info.maxAnisotropy = sampler_info.maxAnisotropy;
	info.compareEnable = sampler_info.compareEnable;
	info.compareOp = sampler_info.compareOp;
	info.minLod = sampler_info.minLod;
	info.maxLod = sampler_info.maxLod;
	info.borderColor = sampler_info.borderColor;
	info.unnormalizedCoordinates = sampler_info.unnormalizedCoordinates;

	VkSampler sampler;
	if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
		return SamplerHandle(nullptr);
	return make_handle<Sampler>(this, sampler, sampler_info);
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

	if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
		return BufferHandle(nullptr);

	vkGetBufferMemoryRequirements(device, buffer, &reqs);

	uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);

	if (!managers.memory.allocate(reqs.size, reqs.alignment, memory_type, ALLOCATION_TILING_LINEAR, &allocation))
	{
		vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	if (vkBindBufferMemory(device, buffer, allocation.get_memory(), allocation.get_offset()) != VK_SUCCESS)
	{
		allocation.free_immediate(managers.memory);
		vkDestroyBuffer(device, buffer, nullptr);
		return BufferHandle(nullptr);
	}

	auto tmpinfo = create_info;
	tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto handle = make_handle<Buffer>(this, buffer, allocation, tmpinfo);

	if (create_info.domain == BufferDomain::Device && (initial || zero_initialize) && !memory_type_is_host_visible(memory_type))
	{
		CommandBufferHandle cmd;
		if (initial)
		{
			auto staging_info = create_info;
			staging_info.domain = BufferDomain::Host;
			auto staging_buffer = create_buffer(staging_info, initial);
			set_name(*staging_buffer, "buffer-upload-staging-buffer");

			cmd = request_command_buffer(CommandBuffer::Type::Transfer);
			cmd->begin_region("copy-buffer-staging");
			cmd->copy_buffer(*handle, *staging_buffer);
			cmd->end_region();
		}
		else
		{
			cmd = request_command_buffer(CommandBuffer::Type::Transfer);
			cmd->begin_region("fill-buffer-staging");
			cmd->fill_buffer(*handle, 0);
			cmd->end_region();
		}

		LOCK();
		submit_staging(cmd, info.usage, true);
	}
	else if (initial || zero_initialize)
	{
		void *ptr = managers.memory.map_memory(&allocation, MEMORY_ACCESS_WRITE);
		if (!ptr)
			return BufferHandle(nullptr);

		if (initial)
			memcpy(ptr, initial, create_info.size);
		else
			memset(ptr, 0, create_info.size);
		managers.memory.unmap_memory(allocation);
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

bool Device::image_format_is_supported(VkFormat format, VkFormatFeatureFlags required) const
{
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
	auto flags = props.optimalTilingFeatures;
	return (flags & required) == required;
}

VkFormat Device::get_default_depth_stencil_format() const
{
	if (image_format_is_supported(VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D24_UNORM_S8_UINT;
	if (image_format_is_supported(VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D32_SFLOAT_S8_UINT;

	return VK_FORMAT_UNDEFINED;
}

VkFormat Device::get_default_depth_format() const
{
	if (image_format_is_supported(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D32_SFLOAT;
	if (image_format_is_supported(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_X8_D24_UNORM_PACK32;
	if (image_format_is_supported(VK_FORMAT_D16_UNORM, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
		return VK_FORMAT_D16_UNORM;

	return VK_FORMAT_UNDEFINED;
}

uint64_t Device::allocate_cookie()
{
	// Reserve lower bits for "special purposes".
	return cookie.fetch_add(16, memory_order_relaxed) + 16;
}

bool Device::enqueue_create_render_pass(Fossilize::Hash hash, unsigned, const VkRenderPassCreateInfo *create_info, VkRenderPass *render_pass)
{
	auto *ret = render_passes.insert(hash, make_unique<RenderPass>(hash, this, *create_info));
	*render_pass = ret->get_render_pass();
	replayer_state.render_pass_map[*render_pass] = ret;
	return true;
}

const RenderPass &Device::request_render_pass(const RenderPassInfo &info)
{
	Hasher h;
	VkFormat formats[VULKAN_NUM_ATTACHMENTS];
	VkFormat depth_stencil;
	uint32_t lazy = 0;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		VK_ASSERT(info.color_attachments[i]);
		formats[i] = info.color_attachments[i]->get_format();
		if (info.color_attachments[i]->get_image().get_create_info().domain == ImageDomain::Transient)
			lazy |= 1u << i;

		h.u32(info.color_attachments[i]->get_image().get_swapchain_layout());
	}

	if (info.depth_stencil && info.depth_stencil->get_image().get_create_info().domain == ImageDomain::Transient)
		lazy |= 1u << info.num_color_attachments;

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
	h.u32(info.op_flags);
	h.u32(info.clear_attachments);
	h.u32(info.load_attachments);
	h.u32(info.store_attachments);
	h.u32(lazy);

	auto hash = h.get();

	auto *ret = render_passes.find(hash);
	if (!ret)
		ret = render_passes.insert(hash, make_unique<RenderPass>(hash, this, info));
	return *ret;
}

const Framebuffer &Device::request_framebuffer(const RenderPassInfo &info)
{
	return framebuffer_allocator.request_framebuffer(info);
}

ImageView &Device::get_transient_attachment(unsigned width, unsigned height, VkFormat format,
                                            unsigned index, unsigned samples)
{
	return transient_allocator.request_attachment(width, height, format, index, samples);
}

ImageView &Device::get_physical_attachment(unsigned width, unsigned height, VkFormat format,
                                           unsigned index, unsigned samples)
{
	return physical_allocator.request_attachment(width, height, format, index, samples);
}

ImageView &Device::get_swapchain_view()
{
	return frame().backbuffer->get_view();
}

RenderPassInfo Device::get_swapchain_render_pass(SwapchainRenderPass style)
{
	RenderPassInfo info;
	info.num_color_attachments = 1;
	info.color_attachments[0] = &frame().backbuffer->get_view();
	info.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT;
	info.clear_attachments = ~0u;
	info.store_attachments = 1u << 0;

	switch (style)
	{
	case SwapchainRenderPass::Depth:
	{
		info.op_flags |= RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT | RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
		info.depth_stencil =
		    &get_transient_attachment(frame().backbuffer->get_create_info().width,
		                              frame().backbuffer->get_create_info().height, get_default_depth_format());
		break;
	}

	case SwapchainRenderPass::DepthStencil:
	{
		info.op_flags |= RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT | RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
		info.depth_stencil =
		    &get_transient_attachment(frame().backbuffer->get_create_info().width,
		                              frame().backbuffer->get_create_info().height, get_default_depth_stencil_format());
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

bool Device::enqueue_create_sampler(Fossilize::Hash hash, unsigned index, const VkSamplerCreateInfo *create_info, VkSampler *sampler)
{
	return false;
}

bool Device::enqueue_create_descriptor_set_layout(Fossilize::Hash, unsigned, const VkDescriptorSetLayoutCreateInfo *, VkDescriptorSetLayout *layout)
{
	// We will create this naturally when building pipelines, can just emit dummy handles.
	*layout = reinterpret_cast<VkDescriptorSetLayout>(uint64_t(-1));
	return true;
}

bool Device::enqueue_create_pipeline_layout(Fossilize::Hash, unsigned, const VkPipelineLayoutCreateInfo *, VkPipelineLayout *layout)
{
	// We will create this naturally when building pipelines, can just emit dummy handles.
	*layout = reinterpret_cast<VkPipelineLayout>(uint64_t(-1));
	return true;
}

void Device::set_name(const Buffer &buffer, const char *name)
{
	if (!ext.supports_debug_marker)
		return;

	VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
	info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT;
	info.object = (uint64_t)buffer.get_buffer();
	info.pObjectName = name;
	vkDebugMarkerSetObjectNameEXT(device, &info);
}

void Device::set_name(const Image &image, const char *name)
{
	if (!ext.supports_debug_marker)
		return;

	VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
	info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
	info.object = (uint64_t)image.get_image();
	info.pObjectName = name;
	vkDebugMarkerSetObjectNameEXT(device, &info);
}

void Device::set_name(const CommandBuffer &cmd, const char *name)
{
	if (!ext.supports_debug_marker)
		return;

	VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
	info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT;
	info.object = (uint64_t)cmd.get_command_buffer();
	info.pObjectName = name;
	vkDebugMarkerSetObjectNameEXT(device, &info);
}

}
