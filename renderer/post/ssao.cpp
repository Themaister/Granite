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

#include "ssao.hpp"
#include "muglm/matrix_helper.hpp"
#include "global_managers.hpp"
#include "common_renderer_data.hpp"

using namespace std;

namespace Granite
{
void setup_ssao(RenderGraph &graph, const RenderContext &context,
                const string &output, const string &input_depth, const string &input_normal)
{
	AttachmentInfo info;
	info.format = VK_FORMAT_R8_UNORM;
	info.size_class = SizeClass::InputRelative;
	info.size_relative_name = input_depth;
	info.size_x = 0.5f;
	info.size_y = 0.5f;

	auto info_blurred = info;
	info_blurred.size_x = 0.25f;
	info_blurred.size_y = 0.25f;

	auto &ssao_first = graph.add_pass(output + "-first", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	auto &depth = ssao_first.add_texture_input(input_depth);
	auto &normal = ssao_first.add_texture_input(input_normal);
	auto &noisy_output = ssao_first.add_color_output(output + "-noise", info);

	ssao_first.set_get_clear_color([](unsigned, VkClearColorValue *value) {
		if (value)
			value->float32[0] = 1.0f;
		return true;
	});

	ssao_first.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		cmd.set_texture(0, 0, graph.get_physical_texture_resource(depth), Vulkan::StockSampler::NearestClamp);
		cmd.set_texture(0, 1, graph.get_physical_texture_resource(normal), Vulkan::StockSampler::NearestClamp);
		cmd.set_texture(0, 2, Global::common_renderer_data()->ssao_luts.noise->get_view(), Vulkan::StockSampler::NearestWrap);
		cmd.set_uniform_buffer(0, 3, *Global::common_renderer_data()->ssao_luts.kernel);

		cmd.set_specialization_constant_mask(3);
		cmd.set_specialization_constant(0, Global::common_renderer_data()->ssao_luts.kernel_size);
		cmd.set_specialization_constant(1, 0.3f);

		struct Push
		{
			mat4 shadow_matrix;
			mat4 inv_view_projection;
			vec4 inv_z_transform;
			vec2 noise_scale;
		};

		auto *push = cmd.allocate_typed_constant_data<Push>(1, 0, 1);
		push->shadow_matrix = translate(vec3(0.5f, 0.5f, 0.0f)) * scale(vec3(0.5f, 0.5f, 1.0f)) * context.get_render_parameters().view_projection;
		push->inv_view_projection = context.get_render_parameters().inv_view_projection;
		push->inv_z_transform = vec4(
				context.get_render_parameters().inv_projection[2].zw(),
				context.get_render_parameters().inv_projection[3].zw());
		push->noise_scale = vec2(
				cmd.get_viewport().width / float(Global::common_renderer_data()->ssao_luts.noise_resolution),
				cmd.get_viewport().height / float(Global::common_renderer_data()->ssao_luts.noise_resolution));

		cmd.push_constants(&push, 0, sizeof(push));
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/post/ssao.vert", "builtin://shaders/post/ssao.frag");
	});

	auto &ssao_blur = graph.add_pass(output + "-blur", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	ssao_blur.add_texture_input(output + "-noise");
	ssao_blur.add_color_output(output, info_blurred);
	ssao_blur.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		cmd.set_texture(0, 0, graph.get_physical_texture_resource(noisy_output), Vulkan::StockSampler::LinearClamp);
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert", "builtin://shaders/post/ssao_blur.frag");
	});
}
}
