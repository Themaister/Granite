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

	auto &ssao = graph.add_pass("ssao", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	auto &depth = ssao.add_texture_input(input_depth);
	auto &normal = ssao.add_texture_input(input_normal);
	ssao.add_color_output(output, info);

	ssao.set_get_clear_color([](unsigned, VkClearColorValue *value) {
		if (value)
			value->float32[0] = 1.0f;
		return true;
	});

	ssao.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		cmd.set_texture(0, 0, graph.get_physical_texture_resource(depth), Vulkan::StockSampler::NearestClamp);
		cmd.set_texture(0, 1, graph.get_physical_texture_resource(normal), Vulkan::StockSampler::NearestClamp);

		struct Push
		{
			mat4 view_projection;
			mat4 shadow_matrix;
			mat4 inv_view_projection;
			vec4 inv_z_transform;
			float radius;
		};

		auto *push = cmd.allocate_typed_constant_data<Push>(1, 0, 1);
		push->view_projection = context.get_render_parameters().view_projection;
		push->shadow_matrix = translate(vec3(0.5f, 0.5f, 0.0f)) * scale(vec3(0.5f, 0.5f, 1.0f)) * context.get_render_parameters().view_projection;
		push->inv_view_projection = context.get_render_parameters().inv_view_projection;
		push->inv_z_transform = vec4(
				context.get_render_parameters().inv_projection[2].zw(),
				context.get_render_parameters().inv_projection[3].zw());
		push->radius = 0.05f;

		cmd.push_constants(&push, 0, sizeof(push));
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert", "builtin://shaders/post/ssao.frag");
	});
}
}
