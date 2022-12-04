/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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
#include "timer.hpp"
#include "thread_group.hpp"

namespace Vulkan
{
#if 0
void Device::register_sampler(VkSampler sampler, Fossilize::Hash hash, const VkSamplerCreateInfo &info)
{
	if (!state_recorder.record_sampler(sampler, info, hash))
		LOGW("Failed to register sampler.\n");
}
#endif

void Device::register_descriptor_set_layout(VkDescriptorSetLayout layout, Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo &info)
{
	if (!state_recorder.record_descriptor_set_layout(layout, info, hash))
		LOGW("Failed to register descriptor set layout.\n");
}

void Device::register_pipeline_layout(VkPipelineLayout layout, Fossilize::Hash hash, const VkPipelineLayoutCreateInfo &info)
{
	if (!state_recorder.record_pipeline_layout(layout, info, hash))
		LOGW("Failed to register pipeline layout.\n");
}

void Device::register_shader_module(VkShaderModule module, Fossilize::Hash hash, const VkShaderModuleCreateInfo &info)
{
	if (!state_recorder.record_shader_module(module, info, hash))
		LOGW("Failed to register shader module.\n");
}

void Device::register_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo &info)
{
	if (!state_recorder.record_compute_pipeline(VK_NULL_HANDLE, info, nullptr, 0, hash))
		LOGW("Failed to register compute pipeline.\n");
}

void Device::register_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo &info)
{
	if (!state_recorder.record_graphics_pipeline(VK_NULL_HANDLE, info, nullptr, 0, hash))
		LOGW("Failed to register graphics pipeline.\n");
}

void Device::register_render_pass(VkRenderPass render_pass, Fossilize::Hash hash, const VkRenderPassCreateInfo &info)
{
	if (!state_recorder.record_render_pass(render_pass, info, hash))
		LOGW("Failed to register render pass.\n");
}

bool Device::enqueue_create_shader_module(Fossilize::Hash hash, const VkShaderModuleCreateInfo *create_info, VkShaderModule *module)
{
	if (!replayer_state.feature_filter->shader_module_is_supported(create_info))
	{
		*module = VK_NULL_HANDLE;
		return true;
	}

	ResourceLayout layout;
	Shader *ret;

	// If we know the resource layout already, just reuse that. Avoids spinning up SPIRV-Cross reflection
	// and allows us to not even build it for release builds.
	if (shader_manager.get_resource_layout_by_shader_hash(hash, layout))
		ret = shaders.emplace_yield(hash, hash, this, create_info->pCode, create_info->codeSize, &layout);
	else
		ret = shaders.emplace_yield(hash, hash, this, create_info->pCode, create_info->codeSize);

	*module = ret->get_module();
	replayer_state.shader_map[*module] = ret;
	return true;
}

void Device::notify_replayed_resources_for_type()
{
#ifdef GRANITE_VULKAN_MT
	if (replayer_state.pipeline_group)
	{
		replayer_state.pipeline_group->wait();
		replayer_state.pipeline_group->release_reference();
		replayer_state.pipeline_group = nullptr;
	}
#endif
}

VkPipeline Device::fossilize_create_graphics_pipeline(Fossilize::Hash hash, VkGraphicsPipelineCreateInfo &info)
{
	if (info.stageCount != 2)
		return VK_NULL_HANDLE;
	if (info.pStages[0].stage != VK_SHADER_STAGE_VERTEX_BIT)
		return VK_NULL_HANDLE;
	if (info.pStages[1].stage != VK_SHADER_STAGE_FRAGMENT_BIT)
		return VK_NULL_HANDLE;

	// Find the Shader* associated with this VkShaderModule and just use that.
	auto vertex_itr = replayer_state.shader_map.find(info.pStages[0].module);
	if (vertex_itr == end(replayer_state.shader_map))
		return VK_NULL_HANDLE;

	// Find the Shader* associated with this VkShaderModule and just use that.
	auto fragment_itr = replayer_state.shader_map.find(info.pStages[1].module);
	if (fragment_itr == end(replayer_state.shader_map))
		return VK_NULL_HANDLE;

	auto *ret = request_program(vertex_itr->second, fragment_itr->second);

	// The layout is dummy, resolve it here.
	info.layout = ret->get_pipeline_layout()->get_layout();

	register_graphics_pipeline(hash, info);

#ifdef VULKAN_DEBUG
	LOGI("Replaying graphics pipeline.\n");
#endif

	uint32_t dynamic_state = 0;
	if (info.pDynamicState)
	{
		for (uint32_t i = 0; i < info.pDynamicState->dynamicStateCount; i++)
		{
			switch (info.pDynamicState->pDynamicStates[i])
			{
			case VK_DYNAMIC_STATE_VIEWPORT:
				dynamic_state |= COMMAND_BUFFER_DIRTY_VIEWPORT_BIT;
				break;

			case VK_DYNAMIC_STATE_SCISSOR:
				dynamic_state |= COMMAND_BUFFER_DIRTY_SCISSOR_BIT;
				break;

			case VK_DYNAMIC_STATE_DEPTH_BIAS:
				dynamic_state |= COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT;
				break;

			case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
			case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
			case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
				dynamic_state |= COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT;
				break;

			default:
				break;
			}
		}
	}

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult res = table->vkCreateGraphicsPipelines(device, pipeline_cache, 1, &info, nullptr, &pipeline);
	if (res != VK_SUCCESS)
		LOGE("Failed to create graphics pipeline!\n");
	return ret->add_pipeline(hash, { pipeline, dynamic_state }).pipeline;
}

VkPipeline Device::fossilize_create_compute_pipeline(Fossilize::Hash hash, VkComputePipelineCreateInfo &info)
{
	// Find the Shader* associated with this VkShaderModule and just use that.
	auto itr = replayer_state.shader_map.find(info.stage.module);
	if (itr == end(replayer_state.shader_map))
		return VK_NULL_HANDLE;

	auto *ret = request_program(itr->second);

	// The layout is dummy, resolve it here.
	info.layout = ret->get_pipeline_layout()->get_layout();

	register_compute_pipeline(hash, info);

#ifdef VULKAN_DEBUG
	LOGI("Replaying compute pipeline.\n");
#endif
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult res = table->vkCreateComputePipelines(device, pipeline_cache, 1, &info, nullptr, &pipeline);
	if (res != VK_SUCCESS)
		LOGE("Failed to create compute pipeline!\n");
	return ret->add_pipeline(hash, { pipeline, 0 }).pipeline;
}

bool Device::enqueue_create_graphics_pipeline(Fossilize::Hash hash,
                                              const VkGraphicsPipelineCreateInfo *create_info,
                                              VkPipeline *pipeline)
{
	for (uint32_t i = 0; i < create_info->stageCount; i++)
	{
		if (create_info->pStages[i].module == VK_NULL_HANDLE)
		{
			*pipeline = VK_NULL_HANDLE;
			return true;
		}
	}

	if (create_info->renderPass == VK_NULL_HANDLE)
	{
		*pipeline = VK_NULL_HANDLE;
		return true;
	}

	if (!replayer_state.feature_filter->graphics_pipeline_is_supported(create_info))
	{
		*pipeline = VK_NULL_HANDLE;
		return true;
	}

#ifdef GRANITE_VULKAN_MT
	if (!replayer_state.pipeline_group && get_system_handles().thread_group)
		replayer_state.pipeline_group = get_system_handles().thread_group->create_task().release();

	if (replayer_state.pipeline_group)
	{
		replayer_state.pipeline_group->enqueue_task([this, create_info, hash, pipeline]() {
			// The lifetime of create_info is tied to the replayer itself.
			auto tmp_create_info = *create_info;
			*pipeline = fossilize_create_graphics_pipeline(hash, tmp_create_info);
		});
		return true;
	}
	else
	{
		auto info = *create_info;
		*pipeline = fossilize_create_graphics_pipeline(hash, info);
		return *pipeline != VK_NULL_HANDLE;
	}
#else
	auto info = *create_info;
	*pipeline = fossilize_create_graphics_pipeline(hash, info);
	return *pipeline != VK_NULL_HANDLE;
#endif
}

bool Device::enqueue_create_compute_pipeline(Fossilize::Hash hash,
                                             const VkComputePipelineCreateInfo *create_info,
                                             VkPipeline *pipeline)
{
	if (create_info->stage.module == VK_NULL_HANDLE)
	{
		*pipeline = VK_NULL_HANDLE;
		return true;
	}

	if (!replayer_state.feature_filter->compute_pipeline_is_supported(create_info))
	{
		*pipeline = VK_NULL_HANDLE;
		return true;
	}

#ifdef GRANITE_VULKAN_MT
	if (!replayer_state.pipeline_group && get_system_handles().thread_group)
		replayer_state.pipeline_group = get_system_handles().thread_group->create_task().release();

	if (replayer_state.pipeline_group)
	{
		replayer_state.pipeline_group->enqueue_task([this, create_info, hash, pipeline]() {
			// The lifetime of create_info is tied to the replayer itself.
			auto tmp_create_info = *create_info;
			*pipeline = fossilize_create_compute_pipeline(hash, tmp_create_info);
		});
		return true;
	}
	else
	{
		auto info = *create_info;
		*pipeline = fossilize_create_compute_pipeline(hash, info);
		return *pipeline != VK_NULL_HANDLE;
	}
#else
	auto info = *create_info;
	*pipeline = fossilize_create_compute_pipeline(hash, info);
	return *pipeline != VK_NULL_HANDLE;
#endif
}

bool Device::enqueue_create_render_pass(Fossilize::Hash hash,
                                        const VkRenderPassCreateInfo *create_info,
                                        VkRenderPass *render_pass)
{
	if (!replayer_state.feature_filter->render_pass_is_supported(create_info))
	{
		*render_pass = VK_NULL_HANDLE;
		return true;
	}

	auto *ret = render_passes.emplace_yield(hash, hash, this, *create_info);
	*render_pass = ret->get_render_pass();
	replayer_state.render_pass_map[*render_pass] = ret;
	return true;
}

bool Device::enqueue_create_render_pass2(Fossilize::Hash, const VkRenderPassCreateInfo2 *, VkRenderPass *)
{
	return false;
}

bool Device::enqueue_create_raytracing_pipeline(
		Fossilize::Hash, const VkRayTracingPipelineCreateInfoKHR *, VkPipeline *)
{
	return false;
}

bool Device::enqueue_create_sampler(Fossilize::Hash, const VkSamplerCreateInfo *, VkSampler *)
{
	//*sampler = get_stock_sampler(static_cast<StockSampler>(hash & 0xffffu)).get_sampler();
	//return true;
	return false;
}

bool Device::enqueue_create_descriptor_set_layout(Fossilize::Hash, const VkDescriptorSetLayoutCreateInfo *, VkDescriptorSetLayout *layout)
{
	// We will create this naturally when building pipelines, can just emit dummy handles.
	*layout = (VkDescriptorSetLayout) uint64_t(-1);
	return true;
}

bool Device::enqueue_create_pipeline_layout(Fossilize::Hash, const VkPipelineLayoutCreateInfo *, VkPipelineLayout *layout)
{
	// We will create this naturally when building pipelines, can just emit dummy handles.
	*layout = (VkPipelineLayout) uint64_t(-1);
	return true;
}

void Device::init_pipeline_state(const Fossilize::FeatureFilter &filter)
{
	replayer_state.feature_filter = &filter;
	state_recorder.init_recording_thread(nullptr);
	if (!get_system_handles().filesystem)
		return;

	auto file = get_system_handles().filesystem->open_readonly_mapping("assets://pipelines.json");
	if (!file)
		file = get_system_handles().filesystem->open_readonly_mapping("cache://pipelines.json");

	if (!file)
		return;

	const void *mapped = file->data();
	if (!mapped)
	{
		LOGE("Failed to map pipelines.json.\n");
		return;
	}

	LOGI("Replaying cached state.\n");
	Fossilize::StateReplayer replayer;
	auto start = Util::get_current_time_nsecs();
	if (!replayer.parse(*this, nullptr, static_cast<const char *>(mapped), file->get_size()))
		LOGE("Failed to parse Fossilize archive.\n");
	auto end = Util::get_current_time_nsecs();
	LOGI("Completed replaying cached state in %.3f ms.\n", (end - start) * 1e-6);

	if (replayer_state.pipeline_group)
	{
		replayer_state.pipeline_group->wait();
		replayer_state.pipeline_group->release_reference();
	}
	replayer_state = {};
	promote_read_write_caches_to_read_only();
}

void Device::flush_pipeline_state()
{
	if (!get_system_handles().filesystem)
		return;

	uint8_t *serialized = nullptr;
	size_t serialized_size = 0;
	if (!state_recorder.serialize(&serialized, &serialized_size))
	{
		LOGE("Failed to serialize Fossilize state.\n");
		return;
	}

	auto file = get_system_handles().filesystem->open("cache://pipelines.json",
	                                                  Granite::FileMode::WriteOnlyTransactional);
	if (file)
	{
		auto mapping = file->map_write(serialized_size);
		auto *data = mapping ? mapping->mutable_data() : nullptr;
		if (data)
			memcpy(data, serialized, serialized_size);
		else
			LOGE("Failed to serialize pipeline data.\n");

	}
	Fossilize::StateRecorder::free_serialized(serialized);
}
}
