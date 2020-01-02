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

#include "temporal.hpp"
#include "fxaa.hpp"
#include "enum_cast.hpp"
#include "muglm/matrix_helper.hpp"
#include "muglm/muglm_impl.hpp"

namespace Granite
{
TemporalJitter::TemporalJitter()
{
	init(Type::None, vec2(0.0f));
}

void TemporalJitter::init(Type type_, vec2 backbuffer_resolution)
{
	type = type_;

	switch (type)
	{
	case Type::FXAA_2Phase:
		jitter_mask = 1;
		phase = 0;
		jitter_table[0] = translate(2.0f * vec3(0.5f / backbuffer_resolution.x, 0.0f, 0.0f));
		jitter_table[1] = translate(2.0f * vec3(0.0f, 0.5f / backbuffer_resolution.y, 0.0f));
		break;

	case Type::SMAA_T2X:
		jitter_mask = 1;
		phase = 0;
		jitter_table[0] = translate(2.0f * vec3(-0.25f / backbuffer_resolution.x, -0.25f / backbuffer_resolution.y, 0.0f));
		jitter_table[1] = translate(2.0f * vec3(+0.25f / backbuffer_resolution.x, +0.25f / backbuffer_resolution.y, 0.0f));
		break;

	case Type::TAA_8Phase:
		jitter_mask = 7;
		phase = 0;
		jitter_table[0] = translate(0.125f * vec3(-7.0f / backbuffer_resolution.x, +1.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[1] = translate(0.125f * vec3(-5.0f / backbuffer_resolution.x, -5.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[2] = translate(0.125f * vec3(-1.0f / backbuffer_resolution.x, -3.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[3] = translate(0.125f * vec3(+3.0f / backbuffer_resolution.x, -7.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[4] = translate(0.125f * vec3(-5.0f / backbuffer_resolution.x, -1.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[5] = translate(0.125f * vec3(+7.0f / backbuffer_resolution.x, +7.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[6] = translate(0.125f * vec3(+1.0f / backbuffer_resolution.x, +3.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[7] = translate(0.125f * vec3(-3.0f / backbuffer_resolution.x, +5.0f / backbuffer_resolution.y, 0.0f));
		break;

	case Type::TAA_16Phase:
		jitter_mask = 15;
		phase = 0;
		jitter_table[ 0] = translate(0.125f * vec3(-8.0f / backbuffer_resolution.x, 0.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 1] = translate(0.125f * vec3(-6.0f / backbuffer_resolution.x, -4.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 2] = translate(0.125f * vec3(-3.0f / backbuffer_resolution.x, -2.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 3] = translate(0.125f * vec3(-2.0f / backbuffer_resolution.x, -6.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 4] = translate(0.125f * vec3(1.0f / backbuffer_resolution.x, -1.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 5] = translate(0.125f * vec3(2.0f / backbuffer_resolution.x, -5.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 6] = translate(0.125f * vec3(6.0f / backbuffer_resolution.x, -7.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 7] = translate(0.125f * vec3(5.0f / backbuffer_resolution.x, -3.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 8] = translate(0.125f * vec3(4.0f / backbuffer_resolution.x, 1.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 9] = translate(0.125f * vec3(7.0f / backbuffer_resolution.x, 4.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[10] = translate(0.125f * vec3(3.0f / backbuffer_resolution.x, 5.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[11] = translate(0.125f * vec3(0.0f / backbuffer_resolution.x, 7.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[12] = translate(0.125f * vec3(-1.0f / backbuffer_resolution.x, 3.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[13] = translate(0.125f * vec3(-4.0f / backbuffer_resolution.x, 6.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[14] = translate(0.125f * vec3(-7.0f / backbuffer_resolution.x, 8.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[15] = translate(0.125f * vec3(-5.0f / backbuffer_resolution.x, 2.0f / backbuffer_resolution.y, 0.0f));
		break;

	case Type::None:
		jitter_mask = 0;
		phase = 0;
		jitter_table[0] = mat4(1.0f);
		break;
	}
}

void TemporalJitter::step(const mat4 &proj, const mat4 &view)
{
	phase++;
	saved_view_proj[get_jitter_phase()] = proj * view;
	saved_inv_view_proj[get_jitter_phase()] = inverse(saved_view_proj[get_jitter_phase()]);
	saved_jittered_view_proj[get_jitter_phase()] = get_jitter_matrix() * saved_view_proj[get_jitter_phase()];
	saved_jittered_inv_view_proj[get_jitter_phase()] = inverse(saved_jittered_view_proj[get_jitter_phase()]);
}

const mat4 &TemporalJitter::get_history_view_proj(int frames) const
{
	return saved_view_proj[(phase - frames) & jitter_mask];
}

const mat4 &TemporalJitter::get_history_inv_view_proj(int frames) const
{
	return saved_inv_view_proj[(phase - frames) & jitter_mask];
}

const mat4 &TemporalJitter::get_history_jittered_view_proj(int frames) const
{
	return saved_jittered_view_proj[(phase - frames) & jitter_mask];
}

const mat4 &TemporalJitter::get_history_jittered_inv_view_proj(int frames) const
{
	return saved_jittered_inv_view_proj[(phase - frames) & jitter_mask];
}

const mat4 &TemporalJitter::get_jitter_matrix() const
{
	return jitter_table[get_jitter_phase()];
}

void TemporalJitter::reset()
{
	phase = 0;
}

unsigned TemporalJitter::get_jitter_phase() const
{
	return phase & jitter_mask;
}

unsigned TemporalJitter::get_unmasked_phase() const
{
	return phase;
}

void setup_taa_resolve(RenderGraph &graph, TemporalJitter &jitter, const std::string &input,
                       const std::string &input_depth, const std::string &output, TAAQuality quality)
{
	jitter.init(TemporalJitter::Type::TAA_16Phase,
	            vec2(graph.get_backbuffer_dimensions().width,
	                 graph.get_backbuffer_dimensions().height));

	AttachmentInfo taa_output;
	taa_output.size_class = SizeClass::InputRelative;
	taa_output.size_relative_name = input;
	taa_output.format = VK_FORMAT_R16G16B16A16_SFLOAT;

#define TAA_MOTION_VECTORS 0

#if TAA_MOTION_VECTORS
	AttachmentInfo mv_output;
	mv_output.size_class = SizeClass::InputRelative;
	mv_output.size_relative_name = input;
	mv_output.format = VK_FORMAT_R16G16_SFLOAT;

	auto &mvs = graph.add_pass("taa-motion-vectors", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	mvs.add_color_output("taa-mvs", mv_output);
	mvs.set_depth_stencil_input(input_depth);
	mvs.add_attachment_input(input_depth);

	mvs.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		cmd.set_input_attachments(0, 0);

		struct Push
		{
			mat4 reproj;
		};
		Push push;

		push.reproj =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				jitter.get_history_view_proj(1) *
				jitter.get_history_inv_view_proj(0);

		cmd.push_constants(&push, 0, sizeof(push));
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
		                                     "builtin://shaders/quad.vert",
		                                     "builtin://shaders/post/depth_to_motion_vectors.frag");

		// Technically, we should also render dynamic objects where appropriate, some day, some day ...
	});
#endif

	auto &resolve = graph.add_pass("taa-resolve", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	resolve.add_color_output(output, taa_output);
	auto &input_res = resolve.add_texture_input(input);
	auto &input_depth_res = resolve.add_texture_input(input_depth);
#if TAA_MOTION_VECTORS
	resolve.add_texture_input("taa-mvs");
#endif
	auto &history = resolve.add_history_input(output);

	resolve.set_build_render_pass([&, q = Util::ecast(quality)](Vulkan::CommandBuffer &cmd) {
		auto &image = graph.get_physical_texture_resource(input_res);
		auto &depth = graph.get_physical_texture_resource(input_depth_res);
#if TAA_MOTION_VECTORS
		auto &mvs = graph.get_physical_texture_resource(resolve.get_texture_inputs()[2]->get_physical_index());
#endif
		auto *prev = graph.get_physical_history_texture_resource(history);

		struct Push
		{
			mat4 reproj;
			vec4 inv_resolution;
		};
		Push push;

		push.reproj =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				jitter.get_history_view_proj(1) *
				jitter.get_history_inv_view_proj(0);

		push.inv_resolution = vec4(1.0f / image.get_image().get_create_info().width,
		                           1.0f / image.get_image().get_create_info().height,
		                           image.get_image().get_create_info().width,
		                           image.get_image().get_create_info().height);

		cmd.push_constants(&push, 0, sizeof(push));

		cmd.set_texture(0, 0, image, Vulkan::StockSampler::NearestClamp);
		cmd.set_texture(0, 1, depth, Vulkan::StockSampler::NearestClamp);
		if (prev)
			cmd.set_texture(0, 2, *prev, Vulkan::StockSampler::LinearClamp);
#if TAA_MOTION_VECTORS
		cmd.set_texture(0, 3, mvs, Vulkan::StockSampler::NearestClamp);
#endif

		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
		                                                "builtin://shaders/quad.vert",
		                                                "builtin://shaders/post/taa_resolve.frag",
		                                                {
				                                                {"REPROJECTION_HISTORY", prev ? 1 : 0},
				                                                {"TAA_QUALITY",          q}
		                                                });
	});
}

void setup_fxaa_2phase_postprocess(RenderGraph &graph, TemporalJitter &jitter, const std::string &input,
                                   const std::string &input_depth, const std::string &output)
{
	jitter.init(TemporalJitter::Type::FXAA_2Phase,
	            vec2(graph.get_backbuffer_dimensions().width, graph.get_backbuffer_dimensions().height));

	setup_fxaa_postprocess(graph, input, "fxaa-pre", VK_FORMAT_R8G8B8A8_UNORM);
	graph.get_texture_resource("fxaa-pre").get_attachment_info().unorm_srgb_alias = true;

	auto &sharpen = graph.add_pass("fxaa-sharpen", RenderGraph::get_default_post_graphics_queue());
	AttachmentInfo att, backbuffer_att;
	att.size_relative_name = input;
	att.size_class = SizeClass::InputRelative;
	att.format = VK_FORMAT_R8G8B8A8_SRGB;
	backbuffer_att = att;
	backbuffer_att.format = VK_FORMAT_UNDEFINED;

	sharpen.add_color_output(output, backbuffer_att);
	sharpen.add_color_output("fxaa-sharpen", att);
	auto &input_res = sharpen.add_texture_input("fxaa-pre");
	auto &depth_res = sharpen.add_texture_input(input_depth);
	auto &history_res = sharpen.add_history_input("fxaa-sharpen");

	sharpen.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto *history = graph.get_physical_history_texture_resource(history_res);
		auto &fxaa = graph.get_physical_texture_resource(input_res);
		auto &depth = graph.get_physical_texture_resource(depth_res);

		struct Push
		{
			mat4 reproj;
			vec2 inv_resolution;
		};
		Push push;

		push.reproj =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				jitter.get_history_view_proj(1) *
				jitter.get_history_inv_view_proj(0);

		push.inv_resolution = vec2(1.0f / fxaa.get_image().get_create_info().width,
		                           1.0f / fxaa.get_image().get_create_info().height);

		auto &output_image = graph.get_physical_texture_resource(sharpen.get_color_outputs()[0]->get_physical_index());
		bool srgb = Vulkan::format_is_srgb(output_image.get_format());
		cmd.set_sampler(0, 0, Vulkan::StockSampler::LinearClamp);
		if (srgb)
			cmd.set_srgb_texture(0, 0, fxaa);
		else
			cmd.set_unorm_texture(0, 0, fxaa);

		if (history)
		{
			cmd.set_texture(0, 1, *history, Vulkan::StockSampler::LinearClamp);
			cmd.set_texture(0, 2, depth, Vulkan::StockSampler::NearestClamp);
		}

		cmd.push_constants(&push, 0, sizeof(push));
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
		                                                "builtin://shaders/post/aa_sharpen_resolve.frag",
		                                                {{"REPROJECTION_HISTORY", history ? 1 : 0},
		                                                 {"HORIZONTAL",           jitter.get_jitter_phase() == 0 ? 1
		                                                                                                         : 0},
		                                                 {"VERTICAL",             jitter.get_jitter_phase() == 1 ? 1
		                                                                                                         : 0}
		                                                });
	});
}
}