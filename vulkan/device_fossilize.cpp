/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

using namespace std;

namespace Vulkan
{
void Device::set_render_pass_handle(unsigned index, VkRenderPass render_pass)
{
	state_recorder.set_render_pass_handle(index, render_pass);
}

void Device::set_descriptor_set_layout_handle(unsigned index, VkDescriptorSetLayout set_layout)
{
	state_recorder.set_descriptor_set_layout_handle(index, set_layout);
}

void Device::set_shader_module_handle(unsigned index, VkShaderModule module)
{
	state_recorder.set_shader_module_handle(index, module);
}

void Device::set_pipeline_layout_handle(unsigned index, VkPipelineLayout layout)
{
	state_recorder.set_pipeline_layout_handle(index, layout);
}

unsigned Device::register_descriptor_set_layout(Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo &info)
{
	lock_guard<mutex> holder{state_recorder_lock};
	return state_recorder.register_descriptor_set_layout(hash, info);
}

unsigned Device::register_pipeline_layout(Fossilize::Hash hash, const VkPipelineLayoutCreateInfo &info)
{
	lock_guard<mutex> holder{state_recorder_lock};
	return state_recorder.register_pipeline_layout(hash, info);
}

unsigned Device::register_shader_module(Fossilize::Hash hash, const VkShaderModuleCreateInfo &info)
{
	lock_guard<mutex> holder{state_recorder_lock};
	return state_recorder.register_shader_module(hash, info);
}

unsigned Device::register_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo &info)
{
	lock_guard<mutex> holder{state_recorder_lock};
	return state_recorder.register_compute_pipeline(hash, info);
}

unsigned Device::register_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo &info)
{
	lock_guard<mutex> holder{state_recorder_lock};
	return state_recorder.register_graphics_pipeline(hash, info);
}

unsigned Device::register_render_pass(Fossilize::Hash hash, const VkRenderPassCreateInfo &info)
{
	lock_guard<mutex> holder{state_recorder_lock};
	return state_recorder.register_render_pass(hash, info);
}

bool Device::enqueue_create_shader_module(Fossilize::Hash hash, unsigned, const VkShaderModuleCreateInfo *create_info, VkShaderModule *module)
{
	auto *ret = shaders.insert(hash, make_unique<Shader>(hash, this, create_info->pCode, create_info->codeSize));
	*module = ret->get_module();
	replayer_state.shader_map[*module] = ret;
	return true;
}

void Device::wait_enqueue()
{
#ifdef GRANITE_VULKAN_MT
	if (replayer_state.pipeline_group)
	{
		replayer_state.pipeline_group->wait();
		replayer_state.pipeline_group.reset();
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

	LOGI("Creating graphics pipeline.\n");
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult res = vkCreateGraphicsPipelines(device, pipeline_cache, 1, &info, nullptr, &pipeline);
	if (res != VK_SUCCESS)
		LOGE("Failed to create graphics pipeline!\n");
	return ret->add_pipeline(hash, pipeline);
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

	LOGI("Creating compute pipeline.\n");
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult res = vkCreateComputePipelines(device, pipeline_cache, 1, &info, nullptr, &pipeline);
	if (res != VK_SUCCESS)
		LOGE("Failed to create compute pipeline!\n");
	return ret->add_pipeline(hash, pipeline);
}

bool Device::enqueue_create_graphics_pipeline(Fossilize::Hash hash, unsigned,
                                              const VkGraphicsPipelineCreateInfo *create_info, VkPipeline *pipeline)
{
#ifdef GRANITE_VULKAN_MT
	if (!replayer_state.pipeline_group)
		replayer_state.pipeline_group = Granite::Global::thread_group()->create_task();

	replayer_state.pipeline_group->enqueue_task([this, info = *create_info, hash, pipeline]() mutable {
		*pipeline = fossilize_create_graphics_pipeline(hash, info);
	});

	return true;
#else
	auto info = *create_info;
	*pipeline = fossilize_create_graphics_pipeline(hash, info);
	return *pipeline != VK_NULL_HANDLE;
#endif
}

bool Device::enqueue_create_compute_pipeline(Fossilize::Hash hash, unsigned,
                                             const VkComputePipelineCreateInfo *create_info, VkPipeline *pipeline)
{
#ifdef GRANITE_VULKAN_MT
	if (!replayer_state.pipeline_group)
		replayer_state.pipeline_group = Granite::Global::thread_group()->create_task();

	replayer_state.pipeline_group->enqueue_task([this, info = *create_info, hash, pipeline]() mutable {
		*pipeline = fossilize_create_compute_pipeline(hash, info);
	});

	return true;
#else
	auto info = *create_info;
	*pipeline = fossilize_create_compute_pipeline(hash, info);
	return *pipeline != VK_NULL_HANDLE;
#endif
}

bool Device::enqueue_create_render_pass(Fossilize::Hash hash, unsigned, const VkRenderPassCreateInfo *create_info, VkRenderPass *render_pass)
{
	auto *ret = render_passes.insert(hash, make_unique<RenderPass>(hash, this, *create_info));
	*render_pass = ret->get_render_pass();
	replayer_state.render_pass_map[*render_pass] = ret;
	return true;
}

bool Device::enqueue_create_sampler(Fossilize::Hash hash, unsigned, const VkSamplerCreateInfo *, VkSampler *sampler)
{
	*sampler = get_stock_sampler(static_cast<StockSampler>(hash)).get_sampler();
	return true;
}

bool Device::enqueue_create_descriptor_set_layout(Fossilize::Hash, unsigned, const VkDescriptorSetLayoutCreateInfo *, VkDescriptorSetLayout *layout)
{
	// We will create this naturally when building pipelines, can just emit dummy handles.
	*layout = (VkDescriptorSetLayout) uint64_t(-1);
	return true;
}

bool Device::enqueue_create_pipeline_layout(Fossilize::Hash, unsigned, const VkPipelineLayoutCreateInfo *, VkPipelineLayout *layout)
{
	// We will create this naturally when building pipelines, can just emit dummy handles.
	*layout = (VkPipelineLayout) uint64_t(-1);
	return true;
}

void Device::init_pipeline_state()
{
	auto file = Granite::Global::filesystem()->open("cache://pipelines.json", Granite::FileMode::ReadOnly);
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
	auto file = Granite::Global::filesystem()->open("cache://pipelines.json", Granite::FileMode::WriteOnly);
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
}