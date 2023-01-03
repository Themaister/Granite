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

#include "device_fossilize.hpp"
#include "timer.hpp"
#include "thread_group.hpp"
#include "fossilize_db.hpp"
#include "dynamic_array.hpp"

namespace Vulkan
{
Device::RecorderState::RecorderState()
{
	recorder_ready.store(false, std::memory_order_relaxed);
}

Device::RecorderState::~RecorderState()
{
}

Device::ReplayerState::ReplayerState()
{
	progress.prepare.store(0, std::memory_order_relaxed);
	progress.modules.store(0, std::memory_order_relaxed);
	progress.pipelines.store(0, std::memory_order_relaxed);
}

Device::ReplayerState::~ReplayerState()
{
}

void Device::register_sampler(VkSampler sampler, Fossilize::Hash hash, const VkSamplerCreateInfo &info)
{
	if (!recorder_state)
		return;

	if (!recorder_state->recorder_ready.load(std::memory_order_acquire))
	{
		LOGW("Attempting to register sampler before recorder is ready.\n");
		return;
	}

	if (!recorder_state->recorder.record_sampler(sampler, info, hash))
		LOGW("Failed to register sampler.\n");
}

void Device::register_sampler_ycbcr_conversion(
		VkSamplerYcbcrConversion ycbcr, const VkSamplerYcbcrConversionCreateInfo &info)
{
	if (!recorder_state)
		return;

	if (!recorder_state->recorder_ready.load(std::memory_order_acquire))
	{
		LOGW("Attempting to register sampler YCbCr conversion before recorder is ready.\n");
		return;
	}

	if (!recorder_state->recorder.record_ycbcr_conversion(ycbcr, info))
		LOGW("Failed to register YCbCr conversion.\n");
}

void Device::register_descriptor_set_layout(VkDescriptorSetLayout layout, Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo &info)
{
	if (!recorder_state)
		return;

	if (!recorder_state->recorder_ready.load(std::memory_order_acquire))
	{
		LOGW("Attempting to register descriptor set layout before recorder is ready.\n");
		return;
	}

	if (!recorder_state->recorder.record_descriptor_set_layout(layout, info, hash))
		LOGW("Failed to register descriptor set layout.\n");
}

void Device::register_pipeline_layout(VkPipelineLayout layout, Fossilize::Hash hash, const VkPipelineLayoutCreateInfo &info)
{
	if (!recorder_state)
		return;

	if (!recorder_state->recorder_ready.load(std::memory_order_acquire))
	{
		LOGW("Attempting to register pipeline layout before recorder is ready.\n");
		return;
	}

	if (!recorder_state->recorder.record_pipeline_layout(layout, info, hash))
		LOGW("Failed to register pipeline layout.\n");
}

void Device::register_shader_module(VkShaderModule module, Fossilize::Hash hash, const VkShaderModuleCreateInfo &info)
{
	if (!recorder_state)
		return;

	if (!recorder_state->recorder_ready.load(std::memory_order_acquire))
	{
		LOGW("Attempting to register shader module before recorder is ready.\n");
		return;
	}

	if (!recorder_state->recorder.record_shader_module(module, info, hash))
		LOGW("Failed to register shader module.\n");
}

void Device::register_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo &info)
{
	if (!recorder_state)
		return;

	if (!recorder_state->recorder_ready.load(std::memory_order_acquire))
	{
		LOGW("Attempting to register compute pipeline before recorder is ready.\n");
		return;
	}

	if (!recorder_state->recorder.record_compute_pipeline(VK_NULL_HANDLE, info, nullptr, 0, hash))
		LOGW("Failed to register compute pipeline.\n");
}

void Device::register_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo &info)
{
	if (!recorder_state)
		return;

	if (!recorder_state->recorder_ready.load(std::memory_order_acquire))
	{
		LOGW("Attempting to register graphics pipeline before recorder is ready.\n");
		return;
	}

	if (!recorder_state->recorder.record_graphics_pipeline(VK_NULL_HANDLE, info, nullptr, 0, hash))
		LOGW("Failed to register graphics pipeline.\n");
}

void Device::register_render_pass(VkRenderPass render_pass, Fossilize::Hash hash, const VkRenderPassCreateInfo2KHR &info)
{
	if (!recorder_state)
		return;

	if (!recorder_state->recorder_ready.load(std::memory_order_acquire))
	{
		LOGW("Attempting to register render pass before recorder is ready.\n");
		return;
	}

	if (!recorder_state->recorder.record_render_pass2(render_pass, info, hash))
		LOGW("Failed to register render pass.\n");
}

bool Device::enqueue_create_shader_module(Fossilize::Hash hash, const VkShaderModuleCreateInfo *create_info, VkShaderModule *module)
{
	if (!replayer_state->feature_filter->shader_module_is_supported(create_info))
	{
		*module = VK_NULL_HANDLE;
		replayer_state->progress.modules.fetch_add(1, std::memory_order_release);
		return true;
	}

	ResourceLayout layout;

	// If we know the resource layout already, just reuse that. Avoids spinning up SPIRV-Cross reflection
	// and allows us to not even build it for release builds.
	if (shader_manager.get_resource_layout_by_shader_hash(hash, layout))
		shaders.emplace_yield(hash, hash, this, create_info->pCode, create_info->codeSize, &layout);
	else
		shaders.emplace_yield(hash, hash, this, create_info->pCode, create_info->codeSize);

	// Resolve the handles later.
	*module = (VkShaderModule)hash;
	replayer_state->progress.modules.fetch_add(1, std::memory_order_release);
	return true;
}

bool Device::fossilize_replay_graphics_pipeline(Fossilize::Hash hash, VkGraphicsPipelineCreateInfo &info)
{
	if (info.stageCount != 2 ||
	    info.pStages[0].stage != VK_SHADER_STAGE_VERTEX_BIT ||
	    info.pStages[1].stage != VK_SHADER_STAGE_FRAGMENT_BIT)
	{
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return false;
	}

	auto *vert_shader = shaders.find((Fossilize::Hash)info.pStages[0].module);
	auto *frag_shader = shaders.find((Fossilize::Hash)info.pStages[1].module);

	if (!vert_shader || !frag_shader)
	{
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return false;
	}

	auto *ret = request_program(vert_shader, frag_shader,
	                            reinterpret_cast<const ImmutableSamplerBank *>(info.layout));

	// The layout is dummy, resolve it here.
	info.layout = ret->get_pipeline_layout()->get_layout();

	// Resolve shader modules.
	const_cast<VkPipelineShaderStageCreateInfo *>(info.pStages)[0].module = vert_shader->get_module();
	const_cast<VkPipelineShaderStageCreateInfo *>(info.pStages)[1].module = frag_shader->get_module();

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
	{
		LOGE("Failed to create graphics pipeline!\n");
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return false;
	}

	auto actual_pipe = ret->add_pipeline(hash, { pipeline, dynamic_state }).pipeline;
	if (actual_pipe != pipeline)
		table->vkDestroyPipeline(device, pipeline, nullptr);

	replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
	return actual_pipe != VK_NULL_HANDLE;
}

bool Device::fossilize_replay_compute_pipeline(Fossilize::Hash hash, VkComputePipelineCreateInfo &info)
{
	// Find the Shader* associated with this VkShaderModule and just use that.
	auto *shader = shaders.find((Fossilize::Hash)info.stage.module);
	if (!shader)
	{
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return false;
	}

	auto *ret = request_program(shader, reinterpret_cast<const ImmutableSamplerBank *>(info.layout));

	// The layout is dummy, resolve it here.
	info.layout = ret->get_pipeline_layout()->get_layout();

	// Resolve shader module.
	info.stage.module = shader->get_module();

#ifdef VULKAN_DEBUG
	LOGI("Replaying compute pipeline.\n");
#endif
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult res = table->vkCreateComputePipelines(device, pipeline_cache, 1, &info, nullptr, &pipeline);
	if (res != VK_SUCCESS)
	{
		LOGE("Failed to create compute pipeline!\n");
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return false;
	}

	auto actual_pipe = ret->add_pipeline(hash, { pipeline, 0 }).pipeline;
	if (actual_pipe != pipeline)
		table->vkDestroyPipeline(device, pipeline, nullptr);

	replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
	return actual_pipe != VK_NULL_HANDLE;
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
			replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
			return true;
		}
	}

	if (create_info->renderPass == VK_NULL_HANDLE || create_info->layout == VK_NULL_HANDLE)
	{
		*pipeline = VK_NULL_HANDLE;
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return true;
	}

	if (!replayer_state->feature_filter->graphics_pipeline_is_supported(create_info))
	{
		*pipeline = VK_NULL_HANDLE;
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return true;
	}

	// The lifetime of create_info is tied to the replayer itself.
	replayer_state->graphics_pipelines.emplace_back(hash, const_cast<VkGraphicsPipelineCreateInfo *>(create_info));
	return true;
}

bool Device::enqueue_create_compute_pipeline(Fossilize::Hash hash,
                                             const VkComputePipelineCreateInfo *create_info,
                                             VkPipeline *pipeline)
{
	if (create_info->stage.module == VK_NULL_HANDLE || create_info->layout == VK_NULL_HANDLE)
	{
		*pipeline = VK_NULL_HANDLE;
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return true;
	}

	if (!replayer_state->feature_filter->compute_pipeline_is_supported(create_info))
	{
		*pipeline = VK_NULL_HANDLE;
		replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
		return true;
	}

	// The lifetime of create_info is tied to the replayer itself.
	replayer_state->compute_pipelines.emplace_back(hash, const_cast<VkComputePipelineCreateInfo *>(create_info));
	return true;
}

bool Device::enqueue_create_render_pass(Fossilize::Hash,
                                        const VkRenderPassCreateInfo *,
                                        VkRenderPass *)
{
	return false;
}

bool Device::enqueue_create_render_pass2(Fossilize::Hash hash, const VkRenderPassCreateInfo2 *create_info, VkRenderPass *render_pass)
{
	if (!replayer_state->feature_filter->render_pass2_is_supported(create_info))
	{
		render_pass = VK_NULL_HANDLE;
		return true;
	}

	auto *pass = render_passes.emplace_yield(hash, hash, this, *create_info);
	*render_pass = pass->get_render_pass();
	return true;
}

bool Device::enqueue_create_raytracing_pipeline(
		Fossilize::Hash, const VkRayTracingPipelineCreateInfoKHR *, VkPipeline *)
{
	return false;
}

bool Device::enqueue_create_sampler(Fossilize::Hash hash, const VkSamplerCreateInfo *info, VkSampler *vk_sampler)
{
	if (!replayer_state->feature_filter->sampler_is_supported(info))
	{
		*vk_sampler = VK_NULL_HANDLE;
		return false;
	}

	const ImmutableYcbcrConversion *ycbcr = nullptr;

	if (info->pNext)
	{
		// YCbCr conversion create infos are replayed inline in Fossilize.
		const auto *ycbcr_info = static_cast<const VkSamplerYcbcrConversionCreateInfo *>(info->pNext);
		if (ycbcr_info && ycbcr_info->sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO)
			ycbcr = request_immutable_ycbcr_conversion(*ycbcr_info);
	}

	auto sampler_info = Sampler::fill_sampler_info(*info);
	auto *samp = immutable_samplers.emplace_yield(hash, hash, this, sampler_info, ycbcr);
	*vk_sampler = reinterpret_cast<VkSampler>(samp);
	return true;
}

bool Device::enqueue_create_descriptor_set_layout(Fossilize::Hash, const VkDescriptorSetLayoutCreateInfo *info, VkDescriptorSetLayout *layout)
{
	if (!replayer_state->feature_filter->descriptor_set_layout_is_supported(info))
	{
		*layout = VK_NULL_HANDLE;
		return true;
	}

	auto &alloc = replayer_state->base_replayer.get_allocator();
	auto *sampler_bank = alloc.allocate_n_cleared<const ImmutableSampler *>(VULKAN_NUM_BINDINGS);
	for (uint32_t i = 0; i < info->bindingCount; i++)
		if (info->pBindings[i].pImmutableSamplers && info->pBindings[i].pImmutableSamplers[0] != VK_NULL_HANDLE)
			sampler_bank[i] = reinterpret_cast<const ImmutableSampler *>(info->pBindings[i].pImmutableSamplers[0]);

	*layout = reinterpret_cast<VkDescriptorSetLayout>(sampler_bank);
	return true;
}

bool Device::enqueue_create_pipeline_layout(Fossilize::Hash, const VkPipelineLayoutCreateInfo *info, VkPipelineLayout *layout)
{
	if (!replayer_state->feature_filter->pipeline_layout_is_supported(info))
	{
		*layout = VK_NULL_HANDLE;
		return true;
	}

	auto &alloc = replayer_state->base_replayer.get_allocator();
	auto *sampler_bank = alloc.allocate_cleared<ImmutableSamplerBank>();
	for (uint32_t i = 0; i < info->setLayoutCount; i++)
	{
		memcpy(sampler_bank->samplers[i],
		       reinterpret_cast<const ImmutableSampler *const *>(info->pSetLayouts[i]),
		       sizeof(sampler_bank->samplers[i]));
	}

	*layout = reinterpret_cast<VkPipelineLayout>(sampler_bank);
	return true;
}

void Device::promote_readonly_db_from_assets() const
{
	auto *fs = get_system_handles().filesystem;

	// We might want to be able to ship a Fossilize database so that we can prime all PSOs up front.
	Granite::FileStat s_cache = {};
	Granite::FileStat s_assets = {};
	bool cache_exists = fs->stat("cache://fossilize/db.foz", s_cache) && s_cache.type == Granite::PathType::File;
	bool assets_exists = fs->stat("assets://fossilize/db.foz", s_assets) && s_assets.type == Granite::PathType::File;

	bool overwrite = false;
	if (assets_exists)
	{
		if (!cache_exists)
		{
			overwrite = true;
		}
		else
		{
			// If an application updates the assets Foz DB for shipping updates, throw the old one away.
			std::string cache_iter, asset_iter;
			if (!fs->read_file_to_string("cache://fossilize/iteration", cache_iter) ||
			    !fs->read_file_to_string("assets://fossilize/iteration", asset_iter) ||
			    cache_iter != asset_iter)
			{
				overwrite = true;
			}
		}
	}

	if (overwrite)
	{
		// The Fossilize DB needs to work with a proper file system. The assets folder is highly virtual by nature.
		auto ro = fs->open_readonly_mapping("assets://fossilize/db.foz");
		if (!ro)
		{
			LOGE("Failed to open readonly Fossilize archive.\n");
			return;
		}

		if (!fs->write_buffer_to_file("cache://fossilize/db.foz", ro->data(), ro->get_size()))
		{
			LOGE("Failed to write to cache://fossilize/db.foz");
			return;
		}

		std::string asset_iter;
		if (fs->read_file_to_string("assets://fossilize/iteration", asset_iter))
			fs->write_string_to_file("cache://fossilize/iteration", asset_iter);
	}
}

void Device::replay_tag_simple(Fossilize::ResourceTag tag)
{
	size_t count = 0;
	replayer_state->db->get_hash_list_for_resource_tag(tag, &count, nullptr);
	std::vector<Fossilize::Hash> hashes(count);
	replayer_state->db->get_hash_list_for_resource_tag(tag, &count, hashes.data());

	Util::DynamicArray<uint8_t> buffer;
	auto &db = *replayer_state->db;
	size_t size = 0;

	for (auto hash : hashes)
	{
		if (!db.read_entry(tag, hash, &size, nullptr, 0))
			continue;
		buffer.reserve(size);
		if (!db.read_entry(tag, hash, &size, buffer.data(), 0))
			continue;
		if (!replayer_state->base_replayer.parse(*this, &db, buffer.data(), size))
			LOGW("Failed to replay object.\n");
	}
}

void Device::promote_write_cache_to_readonly() const
{
	auto *fs = get_system_handles().filesystem;
	auto list = fs->list("cache://fossilize");
	std::vector<std::string> merge_paths_str;
	std::vector<std::string> del_paths_str;
	std::vector<const char *> merge_paths;
	merge_paths_str.reserve(list.size());
	merge_paths.reserve(list.size());
	bool have_read_only = false;

	for (auto &l : list)
	{
		if (l.type != Granite::PathType::File || l.path == "fossilize/iteration" || l.path == "fossilize/TOUCH")
			continue;
		else if (l.path == "fossilize/db.foz")
		{
			have_read_only = true;
			LOGI("Fossilize: Found read-only cache.\n");
			continue;
		}
		else if (l.path == "fossilize/merge.foz")
		{
			del_paths_str.emplace_back("cache://fossilize/merge.foz");
			continue;
		}

		auto p = "cache://" + l.path;
		merge_paths_str.push_back(p);
		del_paths_str.push_back(p);
		LOGI("Fossilize: Found write cache: %s.\n", merge_paths_str.back().c_str());
	}

	if (!have_read_only && merge_paths_str.size() == 1)
	{
		LOGI("Fossilize: No read-cache and one write cache. Replacing directly.\n");
		if (fs->move_replace("cache://fossilize/db.foz", merge_paths_str.front()))
			LOGI("Fossilize: Promoted write-only cache.\n");
		else
			LOGW("Fossilize: Failed to promote write-only cache.\n");
	}
	else if (!merge_paths_str.empty())
	{
		auto append_path = fs->get_filesystem_path("cache://fossilize/merge.foz");
		bool should_merge;

		// Ensure that we have taken exclusive write access to this file.
		// Only one process will be able to pass this test until the file is removed.
		if (have_read_only)
		{
			LOGI("Fossilize: Attempting to merge caches.\n");
			should_merge = fs->move_yield("cache://fossilize/merge.foz", "cache://fossilize/db.foz");
		}
		else
		{
			auto db = std::unique_ptr<Fossilize::DatabaseInterface>(
					Fossilize::create_stream_archive_database(append_path.c_str(), Fossilize::DatabaseMode::ExclusiveOverWrite));
			should_merge = db && db->prepare();
		}

		if (should_merge)
		{
			for (auto &str : merge_paths_str)
			{
				str = fs->get_filesystem_path(str);
				merge_paths.push_back(str.c_str());
			}

			if (Fossilize::merge_concurrent_databases(append_path.c_str(), merge_paths.data(), merge_paths.size()))
			{
				if (fs->move_replace("cache://fossilize/db.foz", "cache://fossilize/merge.foz"))
					LOGI("Fossilize: Successfully merged caches.\n");
				else
					LOGW("Fossilize: Failed to replace existing read-only database.\n");
			}
			else
				LOGW("Fossilize: Failed to merge databases.\n");
		}
		else
			LOGW("Fossilize: Skipping merge due to unexpected error.\n");
	}
	else
		LOGI("Fossilize: No write only files, nothing to do.\n");

	// Cleanup any stale write-only files.
	// This can easily race against concurrent processes, so the cache will likely be destroyed by accident,
	// but that's ok. Running multiple Granite processes concurrently like this is questionable at best.
	for (auto &str : del_paths_str)
		fs->remove(str);
}

void Device::init_pipeline_state(const Fossilize::FeatureFilter &filter,
                                 const VkPhysicalDeviceFeatures2 &pdf2,
                                 const VkApplicationInfo &application_info)
{
	if (!get_system_handles().filesystem)
	{
		LOGW("Filesystem system handle must be provided to use Fossilize.\n");
		return;
	}

	if (!get_system_handles().thread_group)
	{
		LOGW("Thread group system handle must be provided to use Fossilize.\n");
		return;
	}

	replayer_state.reset(new ReplayerState);
	recorder_state.reset(new RecorderState);

	if (!recorder_state->recorder.record_application_info(application_info))
		LOGW("Failed to record application info.\n");
	if (!recorder_state->recorder.record_physical_device_features(&pdf2))
		LOGW("Failed to record PDF2.\n");

	lock.read_only_cache.lock_read();

	replayer_state->feature_filter = &filter;
	auto *group = get_system_handles().thread_group;

	auto shader_manager_task = group->create_task([this]() {
		init_shader_manager_cache();
	});
	shader_manager_task->set_desc("shader-manager-init");

	auto cache_maintenance_task = group->create_task([this]() {
		// Ensure we create the Fossilize cache folder.
		// Also creates a timestamp.
		get_system_handles().filesystem->open("cache://fossilize/TOUCH", Granite::FileMode::WriteOnly);
		replayer_state->progress.prepare.fetch_add(20, std::memory_order_release);
		promote_write_cache_to_readonly();
		replayer_state->progress.prepare.fetch_add(50, std::memory_order_release);
		promote_readonly_db_from_assets();
		replayer_state->progress.prepare.fetch_add(20, std::memory_order_release);
	});
	cache_maintenance_task->set_desc("foz-cache-maintenance");

	auto recorder_kick_task = group->create_task([this]() {
		// Kick off recorder thread.
		auto write_real_path = get_system_handles().filesystem->get_filesystem_path("cache://fossilize/db");
		if (!write_real_path.empty())
		{
			recorder_state->db.reset(Fossilize::create_concurrent_database(
				write_real_path.c_str(), Fossilize::DatabaseMode::Append, nullptr, 0));
			recorder_state->recorder.set_database_enable_application_feature_links(false);
			recorder_state->recorder.init_recording_thread(recorder_state->db.get());
		}
		recorder_state->recorder_ready.store(true, std::memory_order_release);
		replayer_state->progress.prepare.fetch_add(10, std::memory_order_release);
	});
	recorder_kick_task->set_desc("foz-recorder-kick");

	group->add_dependency(*recorder_kick_task, *cache_maintenance_task);

	auto prepare_task = group->create_task([this]() {
		auto *fs = get_system_handles().filesystem;
		auto read_real_path = fs->get_filesystem_path("cache://fossilize/db.foz");
		if (read_real_path.empty())
		{
			replayer_state->progress.modules.store(~0u, std::memory_order_release);
			replayer_state->progress.pipelines.store(~0u, std::memory_order_release);
			return;
		}

		replayer_state->db.reset(
			Fossilize::create_stream_archive_database(read_real_path.c_str(), Fossilize::DatabaseMode::ReadOnly));

		if (replayer_state->db && !replayer_state->db->prepare())
		{
			LOGW("Failed to prepare read-only cache.\n");
			replayer_state->db.reset();
		}

		if (replayer_state->db)
		{
			replay_tag_simple(Fossilize::RESOURCE_SAMPLER);
			replay_tag_simple(Fossilize::RESOURCE_DESCRIPTOR_SET_LAYOUT);
			replay_tag_simple(Fossilize::RESOURCE_PIPELINE_LAYOUT);
			replay_tag_simple(Fossilize::RESOURCE_RENDER_PASS);

			size_t count = 0;

			replayer_state->db->get_hash_list_for_resource_tag(Fossilize::RESOURCE_SHADER_MODULE, &count, nullptr);
			replayer_state->module_hashes.resize(count);
			replayer_state->db->get_hash_list_for_resource_tag(Fossilize::RESOURCE_SHADER_MODULE, &count,
			                                                   replayer_state->module_hashes.data());

			replayer_state->db->get_hash_list_for_resource_tag(Fossilize::RESOURCE_GRAPHICS_PIPELINE, &count, nullptr);
			replayer_state->graphics_hashes.resize(count);
			replayer_state->db->get_hash_list_for_resource_tag(Fossilize::RESOURCE_GRAPHICS_PIPELINE, &count,
			                                                   replayer_state->graphics_hashes.data());

			replayer_state->db->get_hash_list_for_resource_tag(Fossilize::RESOURCE_COMPUTE_PIPELINE, &count, nullptr);
			replayer_state->compute_hashes.resize(count);
			replayer_state->db->get_hash_list_for_resource_tag(Fossilize::RESOURCE_COMPUTE_PIPELINE, &count,
			                                                   replayer_state->compute_hashes.data());

			replayer_state->progress.num_modules = replayer_state->module_hashes.size();
			replayer_state->progress.num_pipelines =
			    replayer_state->graphics_hashes.size() + replayer_state->compute_hashes.size();
		}

		if (replayer_state->progress.num_modules == 0)
			replayer_state->progress.modules.store(~0u, std::memory_order_release);
		if (replayer_state->progress.num_pipelines == 0)
			replayer_state->progress.pipelines.store(~0u, std::memory_order_release);
	});
	prepare_task->set_desc("foz-prepare");

	group->add_dependency(*prepare_task, *recorder_kick_task);

	auto parse_modules_task = group->create_task();
	parse_modules_task->set_desc("foz-parse-modules");
	group->add_dependency(*parse_modules_task, *prepare_task);
	group->add_dependency(*parse_modules_task, *shader_manager_task);

	for (unsigned i = 0; i < NumTasks; i++)
	{
		parse_modules_task->enqueue_task([this, i]() {
			if (!replayer_state->db)
				return;

			Fossilize::StateReplayer module_replayer;
			Util::DynamicArray<uint8_t> buffer;
			auto &db = *replayer_state->db;

			size_t start = (i * replayer_state->module_hashes.size()) / NumTasks;
			size_t end = ((i + 1) * replayer_state->module_hashes.size()) / NumTasks;
			size_t size = 0;

			for (; start < end; start++)
			{
				auto hash = replayer_state->module_hashes[start];
				if (!db.read_entry(Fossilize::RESOURCE_SHADER_MODULE, hash, &size, nullptr, Fossilize::PAYLOAD_READ_CONCURRENT_BIT))
					continue;
				buffer.reserve(size);
				if (!db.read_entry(Fossilize::RESOURCE_SHADER_MODULE, hash, &size, buffer.data(), Fossilize::PAYLOAD_READ_CONCURRENT_BIT))
					continue;

				if (!module_replayer.parse(*this, &db, buffer.data(), size))
				{
					replayer_state->progress.modules.fetch_add(1, std::memory_order_release);
					LOGW("Failed to parse module.\n");
				}
			}
		});
	}

	auto parse_graphics_task = group->create_task([this]() {
		if (!replayer_state->db)
			return;

		auto &replayer = replayer_state->graphics_replayer;
		replayer.copy_handle_references(replayer_state->base_replayer);
		replayer.set_resolve_shader_module_handles(false);

		size_t size = 0;
		auto &db = *replayer_state->db;
		Util::DynamicArray<uint8_t> buffer;

		for (auto hash : replayer_state->graphics_hashes)
		{
			if (!db.read_entry(Fossilize::RESOURCE_GRAPHICS_PIPELINE, hash, &size, nullptr, Fossilize::PAYLOAD_READ_CONCURRENT_BIT))
				continue;
			buffer.reserve(size);
			if (!db.read_entry(Fossilize::RESOURCE_GRAPHICS_PIPELINE, hash, &size, buffer.data(), Fossilize::PAYLOAD_READ_CONCURRENT_BIT))
				continue;

			if (!replayer.parse(*this, &db, buffer.data(), size))
			{
				replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
				LOGW("Failed to parse graphics pipeline.\n");
			}
		}
	});
	parse_graphics_task->set_desc("foz-parse-graphics");
	group->add_dependency(*parse_graphics_task, *prepare_task);

	auto parse_compute_task = group->create_task([this]() {
		if (!replayer_state->db)
			return;

		auto &replayer = replayer_state->compute_replayer;
		replayer.copy_handle_references(replayer_state->base_replayer);
		replayer.set_resolve_shader_module_handles(false);

		size_t size = 0;
		auto &db = *replayer_state->db;
		Util::DynamicArray<uint8_t> buffer;
		for (auto hash : replayer_state->compute_hashes)
		{
			if (!db.read_entry(Fossilize::RESOURCE_COMPUTE_PIPELINE, hash, &size, nullptr, Fossilize::PAYLOAD_READ_CONCURRENT_BIT))
				continue;
			buffer.reserve(size);
			if (!db.read_entry(Fossilize::RESOURCE_COMPUTE_PIPELINE, hash, &size, buffer.data(), Fossilize::PAYLOAD_READ_CONCURRENT_BIT))
				continue;

			if (!replayer.parse(*this, &db, buffer.data(), size))
			{
				replayer_state->progress.pipelines.fetch_add(1, std::memory_order_release);
				LOGW("Failed to parse compute pipeline.\n");
			}
		}
	});
	parse_compute_task->set_desc("foz-parse-compute");
	group->add_dependency(*parse_compute_task, *prepare_task);

	auto compile_graphics_task = group->create_task();
	auto compile_compute_task = group->create_task();
	compile_graphics_task->set_desc("foz-compile-graphics");
	compile_compute_task->set_desc("foz-compile-compute");
	group->add_dependency(*compile_graphics_task, *parse_modules_task);
	group->add_dependency(*compile_graphics_task, *parse_graphics_task);
	group->add_dependency(*compile_compute_task, *parse_modules_task);
	group->add_dependency(*compile_compute_task, *parse_compute_task);
	for (unsigned i = 0; i < NumTasks; i++)
	{
		compile_graphics_task->enqueue_task([this, i]() {
			size_t start = (i * replayer_state->graphics_pipelines.size()) / NumTasks;
			size_t end = ((i + 1) * replayer_state->graphics_pipelines.size()) / NumTasks;
			for (; start < end; start++)
			{
				auto &pipe = replayer_state->graphics_pipelines[start];
				fossilize_replay_graphics_pipeline(pipe.first, *pipe.second);
			}
		});

		compile_compute_task->enqueue_task([this, i]() {
			size_t start = (i * replayer_state->compute_pipelines.size()) / NumTasks;
			size_t end = ((i + 1) * replayer_state->compute_pipelines.size()) / NumTasks;
			for (; start < end; start++)
			{
				auto &pipe = replayer_state->compute_pipelines[start];
				fossilize_replay_compute_pipeline(pipe.first, *pipe.second);
			}
		});
	}

	replayer_state->complete = get_system_handles().thread_group->create_task([this]() {
		LOGI("Fossilize replay completed!\n  Modules: %zu\n  Graphics: %zu\n  Compute: %zu\n",
			 replayer_state->module_hashes.size(),
			 replayer_state->graphics_hashes.size(),
			 replayer_state->compute_hashes.size());
		lock.read_only_cache.unlock_read();
		const auto cleanup = [](Fossilize::StateReplayer &r) {
			r.forget_handle_references();
			r.forget_pipeline_handle_references();
			r.get_allocator().reset();
		};
		cleanup(replayer_state->base_replayer);
		cleanup(replayer_state->graphics_replayer);
		cleanup(replayer_state->compute_replayer);
		replayer_state->graphics_pipelines.clear();
		replayer_state->compute_pipelines.clear();
		replayer_state->module_hashes.clear();
		replayer_state->graphics_hashes.clear();
		replayer_state->compute_hashes.clear();
		replayer_state->db.reset();
	});
	replayer_state->complete->set_desc("foz-replay-complete");
	group->add_dependency(*replayer_state->complete, *compile_graphics_task);
	group->add_dependency(*replayer_state->complete, *compile_compute_task);
	replayer_state->complete->flush();

	replayer_state->module_ready = std::move(parse_modules_task);
	replayer_state->module_ready->flush();

	auto compile_task = group->create_task();
	group->add_dependency(*compile_task, *compile_graphics_task);
	group->add_dependency(*compile_task, *compile_compute_task);
	replayer_state->pipeline_ready = std::move(compile_task);
	replayer_state->pipeline_ready->flush();
}

void Device::flush_pipeline_state()
{
	if (replayer_state)
	{
		if (replayer_state->complete)
			replayer_state->complete->wait();
		replayer_state.reset();
	}

	if (recorder_state)
	{
		recorder_state->recorder.tear_down_recording_thread();
		recorder_state.reset();
	}
}

unsigned Device::query_initialization_progress(InitializationStage status) const
{
	if (!replayer_state)
		return 100;

	switch (status)
	{
	case InitializationStage::CacheMaintenance:
		return replayer_state->progress.prepare.load(std::memory_order_acquire);

	case InitializationStage::ShaderModules:
	{
		unsigned done = replayer_state->progress.modules.load(std::memory_order_acquire);
		// Avoid 0/0.
		if (!done)
			return 0;
		else if (done == ~0u)
			return 100;
		return (100u * done) / replayer_state->progress.num_modules;
	}

	case InitializationStage::Pipelines:
	{
		unsigned done = replayer_state->progress.pipelines.load(std::memory_order_acquire);
		// Avoid 0/0.
		if (!done)
			return 0;
		else if (done == ~0u)
			return 100;
		return (100u * done) / replayer_state->progress.num_pipelines;
	}

	default:
		break;
	}

	return 0;
}

void Device::block_until_shader_module_ready()
{
	if (!replayer_state || !replayer_state->module_ready)
		return;
	replayer_state->module_ready->wait();
}

void Device::block_until_pipeline_ready()
{
	if (!replayer_state || !replayer_state->pipeline_ready)
		return;
	replayer_state->pipeline_ready->wait();
}

void Device::wait_shader_caches()
{
	block_until_pipeline_ready();
}
}
