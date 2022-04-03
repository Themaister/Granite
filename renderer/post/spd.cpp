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

#include "spd.hpp"
#include "device.hpp"
#include <algorithm>

namespace Granite
{
bool supports_single_pass_downsample(Vulkan::Device &device, VkFormat format)
{
	auto &features = device.get_device_features();

	bool supports_full_group =
			device.supports_subgroup_size_log2(true, 2, 7);
	bool supports_compute = (features.subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;

	if (device.get_gpu_properties().limits.maxComputeWorkGroupSize[0] < 256)
		return false;
	if (!features.enabled_features.shaderStorageImageArrayDynamicIndexing)
		return false;

	VkFormatProperties3KHR props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3_KHR };
	device.get_format_properties(format, &props3);
	if ((props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT_KHR) == 0)
		return false;
	if ((props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT_KHR) == 0)
		return false;

	constexpr VkSubgroupFeatureFlags required = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT;
	bool supports_quad_basic = (features.subgroup_properties.supportedOperations & required) == required;
	return supports_full_group && supports_compute && supports_quad_basic;
}

void emit_single_pass_downsample(Vulkan::CommandBuffer &cmd, Vulkan::ImageView &input,
                                 const Vulkan::ImageView **output_mips, unsigned num_mips,
                                 Vulkan::Buffer &counter_buffer, VkDeviceSize counter_buffer_offset,
                                 unsigned num_components,
                                 const vec4 *filter_mod)
{
	cmd.set_program("builtin://shaders/post/ffx-spd/spd.comp",
	                {{"SUBGROUP", 1}, {"LINEAR", 1},
	                 {"COMPONENTS", int(num_components)},
	                 {"FILTER_MOD", int(filter_mod != nullptr)}});

	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);
	cmd.set_storage_buffer(0, 1, counter_buffer, counter_buffer_offset, 4);
	for (unsigned i = 0; i < 12; i++)
		cmd.set_storage_texture(0, 2 + i, *output_mips[std::min(i, num_mips - 1)]);

	if (filter_mod)
		memcpy(cmd.allocate_typed_constant_data<vec4>(1, 0, num_mips), filter_mod, num_mips * sizeof(*filter_mod));

	struct Registers
	{
		uint32_t base_image_resolution[2];
		float inv_resolution[2];
		uint32_t mips;
		uint32_t num_workgroups;
	} push = {};

	push.base_image_resolution[0] = output_mips[0]->get_view_width();
	push.base_image_resolution[1] = output_mips[0]->get_view_height();
	push.inv_resolution[0] = 1.0f / float(input.get_view_width());
	push.inv_resolution[1] = 1.0f / float(input.get_view_height());
	push.mips = num_mips;

	uint32_t wg_x = (input.get_view_width() + 63) / 64;
	uint32_t wg_y = (input.get_view_height() + 63) / 64;
	push.num_workgroups = wg_x * wg_y;
	cmd.push_constants(&push, 0, sizeof(push));

	cmd.enable_subgroup_size_control(true);
	cmd.set_subgroup_size_log2(true, 2, 7);
	cmd.dispatch(wg_x, wg_y, 1);
	cmd.enable_subgroup_size_control(false);
}
}
