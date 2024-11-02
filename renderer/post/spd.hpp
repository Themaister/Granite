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

#include "device.hpp"
#include "command_buffer.hpp"
#include "image.hpp"
#include "math.hpp"
#include <string>

namespace Granite
{
class RenderGraph;
class RenderContext;

bool supports_single_pass_downsample(Vulkan::Device &device, VkFormat format);

enum ReductionMode : int
{
	Color = 0,
	Depth
};

struct SPDInfo
{
	const Vulkan::ImageView *input;
	const Vulkan::ImageView **output_mips;
	unsigned num_mips;
	const Vulkan::Buffer *counter_buffer;
	VkDeviceSize counter_buffer_offset;
	unsigned num_components;
	const vec4 *filter_mod;
	ReductionMode mode;
};

static constexpr unsigned MaxSPDMips = 12;
void emit_single_pass_downsample(Vulkan::CommandBuffer &cmd, const SPDInfo &info);
void setup_depth_hierarchy_pass(RenderGraph &graph, const std::string &input, const std::string &output);
}
