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

#include "ssao.hpp"
#include "muglm/matrix_helper.hpp"
#include "global_managers.hpp"
#include "common_renderer_data.hpp"

using namespace std;

namespace Granite
{
void setup_ssao_interleaved(RenderGraph &graph, const RenderContext &context,
                            const string &output, const string &input_depth, const string &input_normal)
{
	auto &ssao_pass = graph.add_pass(output + "-ssao", RENDER_GRAPH_QUEUE_ASYNC_COMPUTE_BIT);

	AttachmentInfo info;
	info.format = VK_FORMAT_R16_SFLOAT;
	info.size_class = SizeClass::InputRelative;
	info.size_relative_name = input_depth;
	info.size_x = 0.25f;
	info.size_y = 0.25f;
	info.layers = 16;
	info.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	auto &interleaved_linear_depth = ssao_pass.add_storage_texture_output(output + "-interleaved-depth", info);

	info.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	auto &interleaved_linear_normal = ssao_pass.add_storage_texture_output(output + "-interleaved-normal", info);

	info.format = VK_FORMAT_R8_UNORM;
	auto &interleaved_ssao = ssao_pass.add_storage_texture_output(output + "-interleaved", info);
	info.layers = 1;
	auto &deinterleaved_ssao = ssao_pass.add_storage_texture_output(output, info);

	auto &depth = ssao_pass.add_texture_input(input_depth);
	auto &normal = ssao_pass.add_texture_input(input_normal);

	ssao_pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto &d = graph.get_physical_texture_resource(depth);
		auto &n = graph.get_physical_texture_resource(normal);
		auto &interleaved_d = graph.get_physical_texture_resource(interleaved_linear_depth);
		auto &interleaved_n = graph.get_physical_texture_resource(interleaved_linear_normal);
		cmd.set_texture(0, 0, d, Vulkan::StockSampler::NearestClamp);
		cmd.set_texture(0, 1, n, Vulkan::StockSampler::NearestClamp);
		cmd.set_storage_texture(0, 2, interleaved_d);
		cmd.set_storage_texture(0, 3, interleaved_n);

		unsigned padded_width = interleaved_d.get_image().get_width() * 4;
		unsigned padded_height = interleaved_d.get_image().get_height() * 4;
		float inv_padded_width = 1.0f / padded_width;
		float inv_padded_height = 1.0f / padded_height;
		cmd.set_program("builtin://shaders/post/ssao_interleave.comp");

		struct Push
		{
			vec4 inv_z_transform;
			vec2 inv_padded_resolution;
			uvec2 num_threads;
		} push = {};

		push.inv_z_transform = vec4(
				context.get_render_parameters().inv_projection[2].zw(),
				context.get_render_parameters().inv_projection[3].zw());
		push.inv_padded_resolution = vec2(inv_padded_width, inv_padded_height);
		push.num_threads = uvec2(padded_width, padded_height);
		cmd.push_constants(&push, 0, sizeof(push));

		cmd.dispatch((padded_width + 7) / 8, (padded_height + 7) / 8, 1);

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		unsigned layer_width = interleaved_d.get_image().get_width();
		unsigned layer_height = interleaved_d.get_image().get_height();

		struct SSAOData
		{
			mat4 shadow_matrix;
			vec4 inv_z_transform;
			vec4 to_world_x;
			vec4 to_world_y;
			vec4 to_world_z;
			vec4 to_world_base;
			vec2 inv_padded_resolution;
			uvec2 num_threads;
		};

		auto *ssao = cmd.allocate_typed_constant_data<SSAOData>(1, 0, 1);
		ssao->shadow_matrix = translate(vec3(0.5f, 0.5f, 0.0f)) * scale(vec3(0.5f, 0.5f, 1.0f)) * context.get_render_parameters().view_projection;
		ssao->inv_z_transform = vec4(
				context.get_render_parameters().inv_projection[2].zw(),
				context.get_render_parameters().inv_projection[3].zw());
		ssao->to_world_x = context.get_render_parameters().inv_view_projection[0];
		ssao->to_world_y = context.get_render_parameters().inv_view_projection[1];
		ssao->to_world_z = vec4(context.get_render_parameters().camera_front, 0.0f);
		ssao->to_world_base = vec4(context.get_render_parameters().camera_position, 0.0f);
		ssao->inv_padded_resolution = vec2(inv_padded_width, inv_padded_height);
		ssao->num_threads = uvec2(layer_width, layer_height);

		cmd.set_program("builtin://shaders/post/ssao.comp");
		cmd.set_texture(0, 0, interleaved_d, Vulkan::StockSampler::NearestClamp);
		cmd.set_texture(0, 1, interleaved_n, Vulkan::StockSampler::NearestClamp);
		cmd.set_storage_texture(0, 2, graph.get_physical_texture_resource(interleaved_ssao));
		cmd.set_texture(0, 3, Global::common_renderer_data()->ssao_luts.noise->get_view(), Vulkan::StockSampler::NearestWrap);
		cmd.set_uniform_buffer(0, 4, *Global::common_renderer_data()->ssao_luts.kernel);
		cmd.set_specialization_constant_mask(3);
		cmd.set_specialization_constant(0, Global::common_renderer_data()->ssao_luts.kernel_size);
		cmd.set_specialization_constant(1, 0.1f);
		cmd.dispatch((layer_width + 7) / 8, (layer_height + 7) / 8, 16);

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		cmd.set_program("builtin://shaders/post/ssao_deinterleave.comp");
		cmd.set_texture(0, 0, graph.get_physical_texture_resource(interleaved_ssao), Vulkan::StockSampler::NearestClamp);
		cmd.set_storage_texture(0, 1, graph.get_physical_texture_resource(deinterleaved_ssao));
		uvec2 final_resolution(layer_width, layer_height);
		cmd.push_constants(&final_resolution, 0, sizeof(final_resolution));
		cmd.dispatch((layer_width + 7) / 8, (layer_height + 7) / 8, 16);
	});
}

void setup_ssao_naive(RenderGraph &graph, const RenderContext &context,
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

	RenderTextureResource *normal = nullptr;
	if (!input_normal.empty())
		normal = &ssao_first.add_texture_input(input_normal);
	auto &noisy_output = ssao_first.add_color_output(output + "-noise", info);

	ssao_first.set_get_clear_color([](unsigned, VkClearColorValue *value) {
		if (value)
			value->float32[0] = 1.0f;
		return true;
	});

	ssao_first.set_build_render_pass([&, normal](Vulkan::CommandBuffer &cmd) {
		cmd.set_texture(0, 0, graph.get_physical_texture_resource(depth), Vulkan::StockSampler::NearestClamp);
		if (normal)
			cmd.set_texture(0, 1, graph.get_physical_texture_resource(*normal), Vulkan::StockSampler::NearestClamp);
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
			vec4 dx_clip;
			vec4 dy_clip;
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
		push->dx_clip = context.get_render_parameters().inv_view_projection[0] * (1.0f / cmd.get_viewport().width);
		push->dy_clip = context.get_render_parameters().inv_view_projection[1] * (1.0f / cmd.get_viewport().height);

		cmd.push_constants(&push, 0, sizeof(push));
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/post/ssao.vert", "builtin://shaders/post/ssao.frag",
		                                                {{ "NORMAL", normal ? 1 : 0 }});
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
