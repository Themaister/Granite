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

#include "device.hpp"
#include "thread_group.hpp"

namespace Vulkan
{
struct Device::RecorderState
{
	RecorderState();
	~RecorderState();

	std::unique_ptr<Fossilize::DatabaseInterface> db;
	Fossilize::StateRecorder recorder;
	std::atomic_bool recorder_ready;
};

static constexpr unsigned NumTasks = 4;
struct Device::ReplayerState
{
	ReplayerState();
	~ReplayerState();

	std::vector<Fossilize::Hash> module_hashes;
	std::vector<Fossilize::Hash> graphics_hashes;
	std::vector<Fossilize::Hash> compute_hashes;

	Fossilize::StateReplayer base_replayer;
	Fossilize::StateReplayer graphics_replayer;
	Fossilize::StateReplayer compute_replayer;
	const Fossilize::FeatureFilter *feature_filter = nullptr;
	std::unique_ptr<Fossilize::DatabaseInterface> db;
	Granite::TaskGroupHandle complete;
	Granite::TaskGroupHandle module_ready;
	Granite::TaskGroupHandle pipeline_ready;
	std::vector<std::pair<Fossilize::Hash, VkGraphicsPipelineCreateInfo *>> graphics_pipelines;
	std::vector<std::pair<Fossilize::Hash, VkComputePipelineCreateInfo *>> compute_pipelines;

	struct
	{
		std::atomic_uint32_t pipelines;
		std::atomic_uint32_t modules;
		std::atomic_uint32_t prepare;
		uint32_t num_pipelines = 0;
		uint32_t num_modules = 0;
	} progress;
};
}
