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
void TemporalJitter::init(Type type, vec2 backbuffer_resolution)
{
	switch (type)
	{
	case Type::FXAA_2Phase:
		jitter_mask = 1;
		phase = 0;
		jitter_table[0] = translate(2.0f * vec3(0.5f / backbuffer_resolution.x, 0.0f, 0.0f));
		jitter_table[1] = translate(2.0f * vec3(0.0f, 0.5f / backbuffer_resolution.y, 0.0f));
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
	att.format = VK_FORMAT_R8G8B8A8_UNORM;
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
			vec2 offset_filter_lo;
			vec2 offset_filter_mid;
			vec2 offset_filter_hi;
			vec2 offset_filter_next;
		};
		Push push;

		push.reproj =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				jitter.get_history_view_proj(1) *
				jitter.get_history_inv_view_proj(0);

		if (jitter.get_jitter_phase() == 0)
		{
			push.offset_filter_lo = vec2(-1.0f / fxaa.get_image().get_create_info().width, 0.0f);
			push.offset_filter_mid = vec2(+0.5f / fxaa.get_image().get_create_info().width, 0.0f);
			push.offset_filter_hi = vec2(+2.0f / fxaa.get_image().get_create_info().width, 0.0f);
			push.offset_filter_next = vec2(+1.0f / fxaa.get_image().get_create_info().width, 0.0f);
		}
		else
		{
			push.offset_filter_lo = vec2(0.0f, -1.0f / fxaa.get_image().get_create_info().height);
			push.offset_filter_mid = vec2(0.0f, +0.5f / fxaa.get_image().get_create_info().height);
			push.offset_filter_hi = vec2(0.0f, +2.0f / fxaa.get_image().get_create_info().height);
			push.offset_filter_next = vec2(0.0f, +1.0f / fxaa.get_image().get_create_info().height);
		}

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
		Vulkan::CommandBufferUtil::draw_quad(cmd, "builtin://shaders/quad.vert", "builtin://shaders/post/fxaa_sharpen.frag",
		                                     {{ "HISTORY", history ? 1 : 0 }});
	});
}
}