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

#include "spd.hpp"
#include "device.hpp"
#include "render_graph.hpp"
#include "render_context.hpp"
#include <algorithm>

namespace Granite
{
bool supports_single_pass_downsample(Vulkan::Device &device, VkFormat format)
{
	auto &features = device.get_device_features();

	bool supports_full_group =
			device.supports_subgroup_size_log2(true, 2, 7);
	bool supports_compute = (features.vk11_props.subgroupSupportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;

	if (device.get_gpu_properties().limits.maxComputeWorkGroupSize[0] < 256)
		return false;
	if (!features.enabled_features.shaderStorageImageArrayDynamicIndexing)
		return false;

	VkFormatProperties3 props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
	device.get_format_properties(format, &props3);
	if ((props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT) == 0)
		return false;
	if ((props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT) == 0)
		return false;

	constexpr VkSubgroupFeatureFlags required = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT;
	bool supports_quad_basic = (features.vk11_props.subgroupSupportedOperations & required) == required;
	return supports_full_group && supports_compute && supports_quad_basic;
}

void emit_single_pass_downsample(Vulkan::CommandBuffer &cmd, const SPDInfo &info)
{
	cmd.set_program("builtin://shaders/post/ffx-spd/spd.comp",
	                {{"SUBGROUP", 1},
	                 {"SINGLE_INPUT_TAP", 1},
	                 {"COMPONENTS", int(info.num_components)},
	                 {"FILTER_MOD", int(info.filter_mod != nullptr)},
	                 {"REDUCTION_MODE", Util::ecast(info.mode) }});

	const Vulkan::StockSampler stock = info.mode == ReductionMode::Depth ?
			Vulkan::StockSampler::NearestClamp : Vulkan::StockSampler::LinearClamp;

	cmd.set_texture(0, 0, *info.input, stock);
	cmd.set_storage_buffer(0, 1, *info.counter_buffer, info.counter_buffer_offset, 4);
	for (unsigned i = 0; i < MaxSPDMips; i++)
		cmd.set_storage_texture(0, 2 + i, *info.output_mips[std::min(i, info.num_mips - 1)]);

	if (info.filter_mod)
	{
		memcpy(cmd.allocate_typed_constant_data<vec4>(1, 0, info.num_mips),
		       info.filter_mod, info.num_mips * sizeof(*info.filter_mod));
	}

	struct Registers
	{
		uint32_t base_image_resolution[2];
		float inv_resolution[2];
		uint32_t mips;
		uint32_t num_workgroups;
	} push = {};

	push.base_image_resolution[0] = info.output_mips[0]->get_view_width();
	push.base_image_resolution[1] = info.output_mips[0]->get_view_height();
	push.inv_resolution[0] = 1.0f / float(info.input->get_view_width());
	push.inv_resolution[1] = 1.0f / float(info.input->get_view_height());
	push.mips = info.num_mips;

	uint32_t wg_x = (push.base_image_resolution[0] + 31) / 32;
	uint32_t wg_y = (push.base_image_resolution[1] + 31) / 32;
	push.num_workgroups = wg_x * wg_y;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.enable_subgroup_size_control(true);
	cmd.set_subgroup_size_log2(true, 2, 7);
	cmd.dispatch(wg_x, wg_y, 1);
	cmd.enable_subgroup_size_control(false);
}

struct SPDPassState : RenderPassInterface
{
	RenderTextureResource *otex_resource;
	RenderTextureResource *itex_resource;
	RenderBufferResource *counter_resource;
	Util::SmallVector<Vulkan::ImageViewHandle, MaxSPDMips> views;
	const Vulkan::ImageView *output_mips[MaxSPDMips];
	SPDInfo info = {};

	void build_render_pass(Vulkan::CommandBuffer &cmd) override
	{
		emit_single_pass_downsample(cmd, info);
	}

	void enqueue_prepare_render_pass(RenderGraph &graph, TaskComposer &) override
	{
		auto &otex = graph.get_physical_texture_resource(*otex_resource);

		// If output is part of history, this could happen.
		if (!views.empty() && otex.get_image().get_cookie() != views.front()->get_image().get_cookie())
			views.clear();

		if (views.empty())
		{
			info.num_mips = otex.get_image().get_create_info().levels;
			views.reserve(info.num_mips);
			VK_ASSERT(info.num_mips <= MaxSPDMips);

			Vulkan::ImageViewCreateInfo view_info = {};
			view_info.image = &otex.get_image();
			view_info.levels = 1;
			view_info.layers = 1;
			view_info.format = VK_FORMAT_R32_SFLOAT;
			view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;

			for (unsigned i = 0; i < info.num_mips; i++)
			{
				view_info.base_level = i;
				views.push_back(graph.get_device().create_image_view(view_info));
				output_mips[i] = views.back().get();
			}
			info.output_mips = output_mips;
		}

		info.input = &graph.get_physical_texture_resource(*itex_resource);
		info.counter_buffer = &graph.get_physical_buffer_resource(*counter_resource);
		info.num_components = 1;
		info.mode = ReductionMode::Depth;
	}
};

void setup_depth_hierarchy_pass(RenderGraph &graph, const std::string &input, const std::string &output)
{
	AttachmentInfo att;
	att.format = VK_FORMAT_R32_SFLOAT;
	att.size_relative_name = input;
	att.size_class = SizeClass::InputRelative;

	auto &pass = graph.add_pass(output, RENDER_GRAPH_QUEUE_COMPUTE_BIT);
	auto handle = Util::make_handle<SPDPassState>();
	handle->itex_resource = &pass.add_texture_input(input);

	// Stop if we reach 2x1 or 1x2 dimension.
	auto dim = graph.get_resource_dimensions(*handle->itex_resource);
	att.levels = Util::floor_log2(std::min(dim.width, dim.height)) + 1;
	handle->otex_resource = &pass.add_storage_texture_output(output, att);

	BufferInfo storage_info = {};
	storage_info.size = 4;
	storage_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	handle->counter_resource = &pass.add_storage_output(output + "-counter", storage_info);

	pass.set_render_pass_interface(std::move(handle));
}
}
