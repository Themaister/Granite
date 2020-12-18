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

#include "fxaa.hpp"
#include "math.hpp"

namespace Granite
{
void setup_fxaa_postprocess(RenderGraph &graph, const std::string &input, const std::string &output, VkFormat output_format)
{
	graph.get_texture_resource(input).get_attachment_info().unorm_srgb_alias = true;

	auto &fxaa = graph.add_pass("fxaa", RenderGraph::get_default_post_graphics_queue());
	AttachmentInfo fxaa_output;
	fxaa_output.supports_prerotate = true;
	fxaa_output.size_class = SizeClass::InputRelative;
	fxaa_output.size_relative_name = input;
	fxaa_output.format = output_format;

	fxaa.add_color_output(output, fxaa_output);
	auto &fxaa_input = fxaa.add_texture_input(input);
	fxaa.set_build_render_pass([&, input](Vulkan::CommandBuffer &cmd) {
		auto &input_image = graph.get_physical_texture_resource(fxaa_input);
		cmd.set_unorm_texture(0, 0, input_image);
		cmd.set_sampler(0, 0, Vulkan::StockSampler::LinearClamp);
		vec2 inv_size(1.0f / input_image.get_image().get_create_info().width,
		              1.0f / input_image.get_image().get_create_info().height);
		cmd.push_constants(&inv_size, 0, sizeof(inv_size));

		auto &output_image = graph.get_physical_texture_resource(fxaa.get_color_outputs()[0]->get_physical_index());
		bool srgb = Vulkan::format_is_srgb(output_image.get_format());

		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
		                                                "builtin://shaders/post/fxaa.frag",
		                                                {{"FXAA_TARGET_SRGB", srgb ? 1 : 0}});
	});
}
}