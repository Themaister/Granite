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

#include "render_graph.hpp"
#include "render_context.hpp"
#include "spd.hpp"
#include "command_buffer.hpp"
#include "ssr.hpp"

namespace Granite
{
static void screenspace_trace(Vulkan::CommandBuffer &cmd,
                              const Vulkan::ImageView &output,
                              const Vulkan::ImageView &depth,
                              const Vulkan::ImageView &normal,
                              const Vulkan::ImageView &pbr,
                              const RenderContext &context)
{
	cmd.set_program("builtin://shaders/post/screenspace_trace.comp");
	cmd.set_texture(0, 0, depth);
	cmd.set_texture(0, 1, normal);
	cmd.set_texture(0, 2, pbr);
	cmd.set_storage_texture(0, 3, output);

	struct UBO
	{
		alignas(16) mat4 view_projection;
		alignas(16) mat4 inv_view_projection;
		alignas(8) vec2 float_resolution;
		alignas(8) vec2 inv_resolution;
		alignas(8) uvec2 resolution;
		alignas(4) uint32_t max_lod;
		alignas(16) vec3 camera_position;
	};

	auto *ubo = cmd.allocate_typed_constant_data<UBO>(1, 0, 1);
	ubo->view_projection = context.get_render_parameters().view_projection;
	ubo->inv_view_projection = context.get_render_parameters().inv_view_projection;
	vec2 float_resolution(float(output.get_view_width()), float(output.get_view_height()));
	ubo->float_resolution = float_resolution;
	ubo->inv_resolution = 1.0f / float_resolution;
	ubo->resolution = uvec2(output.get_view_width(), output.get_view_height());
	ubo->camera_position = context.get_render_parameters().camera_position;
	ubo->max_lod = output.get_create_info().levels - 1;

	cmd.dispatch((output.get_view_width() + 7) / 8, (output.get_view_height() + 7) / 8, 1);
}

void setup_ssr_pass(RenderGraph &graph, const RenderContext &context,
                    const std::string &input_depth, const std::string &input_normal,
                    const std::string &input_pbr,
                    const std::string &output)
{
	setup_depth_hierarchy_pass(graph, input_depth, input_depth + "-hier");

	AttachmentInfo att;
	att.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	att.size_class = SizeClass::InputRelative;
	att.size_relative_name = input_depth;

	auto &pass = graph.add_pass(output, RENDER_GRAPH_QUEUE_COMPUTE_BIT);
	auto &normal = pass.add_texture_input(input_normal);
	auto &pbr = pass.add_texture_input(input_pbr);
	auto &depth = pass.add_texture_input(input_depth + "-hier");
	auto &uv = pass.add_storage_texture_output(output, att);

	pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		screenspace_trace(cmd,
						  graph.get_physical_texture_resource(uv),
						  graph.get_physical_texture_resource(depth),
						  graph.get_physical_texture_resource(normal),
						  graph.get_physical_texture_resource(pbr),
						  context);
	});
}

static void screenspace_resolve(Vulkan::CommandBuffer &cmd, const Vulkan::ImageView &output,
                                const Vulkan::ImageView &input,
                                const Vulkan::ImageView &ssr)
{
	cmd.set_program("builtin://shaders/post/screenspace_reflect_resolve.comp");
	cmd.set_texture(0, 0, input);
	cmd.set_texture(0, 1, ssr);
	cmd.set_sampler(0, 2, Vulkan::StockSampler::LinearClamp);
	cmd.set_storage_texture(0, 3, output);

	struct Push
	{
		uvec2 resolution;
	} push = {};
	push.resolution = uvec2(output.get_view_width(), output.get_view_height());
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((output.get_view_width() + 7) / 8, (output.get_view_height() + 7) / 8, 1);
}

void setup_ssr_resolve_pass(RenderGraph &graph, const std::string &input, const std::string &ssr,
                            const std::string &output)
{
	auto &pass = graph.add_pass("SSR-resolve", RENDER_GRAPH_QUEUE_COMPUTE_BIT);
	auto &input_tex = pass.add_texture_input(input);
	auto &ssr_tex = pass.add_texture_input(ssr);

	AttachmentInfo att;
	att.format = graph.get_resource_dimensions(input_tex).format;
	att.size_relative_name = input;
	att.size_class = SizeClass::InputRelative;
	auto &output_tex = pass.add_storage_texture_output(output, att);

	pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		screenspace_resolve(cmd,
		                    graph.get_physical_texture_resource(output_tex),
		                    graph.get_physical_texture_resource(input_tex),
		                    graph.get_physical_texture_resource(ssr_tex));
	});
}
}