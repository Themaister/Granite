/* Copyright (c) 2017 Hans-Kristian Arntzen
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

namespace Granite
{
TemporalJitter::TemporalJitter()
{
	init(Type::None, vec2(0.0f));
}

void TemporalJitter::init(Type type, vec2 backbuffer_resolution)
{
	switch (type)
	{
	case Type::FXAA_2Phase:
	case Type::SMAA_2Phase:
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

	case Type::None:
		jitter_mask = 0;
		phase = 0;
		jitter_table[0] = mat4(1.0f);
		break;
	}

	this->type = type;
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

void setup_taa_resolve(RenderGraph &graph, TemporalJitter &jitter, const std::string &input,
                       const std::string &input_depth, const std::string &output)
{
	jitter.init(TemporalJitter::Type::TAA_8Phase,
	            vec2(graph.get_backbuffer_dimensions().width,
	                 graph.get_backbuffer_dimensions().height));

	AttachmentInfo taa_output;
	taa_output.size_class = SizeClass::InputRelative;
	taa_output.size_relative_name = input;
	taa_output.format = VK_FORMAT_R16G16B16A16_SFLOAT;

#define TAA_MOTION_VECTORS 1

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
		Vulkan::CommandBufferUtil::set_quad_vertex_state(cmd);
		Vulkan::CommandBufferUtil::draw_quad(cmd,
		                                     "builtin://shaders/quad.vert",
		                                     "builtin://shaders/post/depth_to_motion_vectors.frag",
		                                     {});

		// Technically, we should also render dynamic objects where appropriate, some day, some day ...
	});
#endif

#define TAA_PRECOMPUTE_TONEMAP 0

#if TAA_PRECOMPUTE_TONEMAP
	auto taa_ycgco = taa_output;
	taa_ycgco.format = VK_FORMAT_R16G16B16A16_SFLOAT;

	auto &conv_ycgco = graph.add_pass("taa-ycgco-conv", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	conv_ycgco.add_color_output("taa-ycgco", taa_ycgco);
	conv_ycgco.add_attachment_input(input);

	conv_ycgco.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		cmd.set_input_attachments(0, 0);
		Vulkan::CommandBufferUtil::draw_quad(cmd,
		                                     "builtin://shaders/quad.vert",
		                                     "builtin://shaders/post/ycgco_conv.frag", {});
	});
#endif

	auto &resolve = graph.add_pass("taa-resolve", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	resolve.add_color_output(output, taa_output);
#if TAA_PRECOMPUTE_TONEMAP
	resolve.add_texture_input("taa-ycgco");
#else
	resolve.add_texture_input(input);
#endif
	resolve.add_texture_input(input_depth);
#if TAA_MOTION_VECTORS
	resolve.add_texture_input("taa-mvs");
#endif
	resolve.add_history_input(output);

	resolve.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto &image = graph.get_physical_texture_resource(resolve.get_texture_inputs()[0]->get_physical_index());
		auto &depth = graph.get_physical_texture_resource(resolve.get_texture_inputs()[1]->get_physical_index());
#if TAA_MOTION_VECTORS
		auto &mvs = graph.get_physical_texture_resource(resolve.get_texture_inputs()[2]->get_physical_index());
#endif
		auto *prev = graph.get_physical_history_texture_resource(resolve.get_history_inputs()[0]->get_physical_index());

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

		Vulkan::CommandBufferUtil::set_quad_vertex_state(cmd);
		Vulkan::CommandBufferUtil::draw_quad(cmd,
		                                     "builtin://shaders/quad.vert",
		                                     "builtin://shaders/post/taa_resolve.frag",
		                                     {{ "REPROJECTION_HISTORY", prev ? 1 : 0 }});
	});
}

void setup_fxaa_2phase_postprocess(RenderGraph &graph, TemporalJitter &jitter, const std::string &input,
                                   const std::string &input_depth, const std::string &output)
{
	jitter.init(TemporalJitter::Type::FXAA_2Phase,
	            vec2(graph.get_backbuffer_dimensions().width, graph.get_backbuffer_dimensions().height));

	setup_fxaa_postprocess(graph, input, "fxaa-pre", VK_FORMAT_R8G8B8A8_UNORM);
	graph.get_texture_resource("fxaa-pre").get_attachment_info().unorm_srgb_alias = true;

	auto &sharpen = graph.add_pass("fxaa-sharpen", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	AttachmentInfo att, backbuffer_att;
	att.size_relative_name = input;
	att.size_class = SizeClass::InputRelative;
	att.format = VK_FORMAT_R8G8B8A8_SRGB;
	backbuffer_att = att;
	backbuffer_att.format = VK_FORMAT_UNDEFINED;

	sharpen.add_color_output(output, backbuffer_att);
	sharpen.add_color_output("fxaa-sharpen", att);
	sharpen.add_texture_input("fxaa-pre");
	sharpen.add_texture_input(input_depth);
	sharpen.add_history_input("fxaa-sharpen");

	sharpen.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto *history = graph.get_physical_history_texture_resource(
				sharpen.get_history_inputs()[0]->get_physical_index());
		auto &fxaa = graph.get_physical_texture_resource(sharpen.get_texture_inputs()[0]->get_physical_index());
		auto &depth = graph.get_physical_texture_resource(sharpen.get_texture_inputs()[1]->get_physical_index());

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
		Vulkan::CommandBufferUtil::set_quad_vertex_state(cmd);
		Vulkan::CommandBufferUtil::draw_quad(cmd, "builtin://shaders/quad.vert", "builtin://shaders/post/aa_sharpen_resolve.frag",
		                                     {{ "HISTORY", history ? 1 : 0 },
		                                      { "HORIZONTAL", jitter.get_jitter_phase() == 0 ? 1 : 0 },
		                                      { "VERTICAL", jitter.get_jitter_phase() == 1 ? 1 : 0 }
		                                     });
	});
}
}