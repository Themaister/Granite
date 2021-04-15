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

#include "aa.hpp"
#include "temporal.hpp"
#include "fxaa.hpp"
#include "smaa.hpp"
#include <string.h>

namespace Granite
{
constexpr bool upscale_linear = false;

bool setup_after_post_chain_upscaling(RenderGraph &graph, const std::string &input, const std::string &output)
{
	auto &upscale = graph.add_pass(output, RenderGraph::get_default_post_graphics_queue());
	AttachmentInfo info;
	info.supports_prerotate = true;
	auto &tex_out = upscale.add_color_output(output, info);

	auto *fs = GRANITE_FILESYSTEM();
	FileStat s;
	bool use_custom = fs->stat("assets://shaders/upscale.vert", s) && s.type == PathType::File &&
	                  fs->stat("assets://shaders/upscale.frag", s) && s.type == PathType::File;

	if (!upscale_linear)
		graph.get_texture_resource(input).get_attachment_info().unorm_srgb_alias = true;

	auto &tex = upscale.add_texture_input(input);

	upscale.set_build_render_pass([&, use_custom](Vulkan::CommandBuffer &cmd) {
		auto &view = graph.get_physical_texture_resource(tex);
		if (upscale_linear)
			cmd.set_texture(0, 0, view);
		else
			cmd.set_unorm_texture(0, 0, view);
		cmd.set_sampler(0, 0, Vulkan::StockSampler::NearestClamp);

		auto *params = cmd.allocate_typed_constant_data<vec4>(1, 0, 2);

		auto width = float(view.get_image().get_width());
		auto height = float(view.get_image().get_height());
		params[0] = vec4(width, height, 1.0f / width, 1.0f / height);

		width = cmd.get_viewport().width;
		height = cmd.get_viewport().height;
		params[1] = vec4(width, height, 1.0f / width, 1.0f / height);

		bool srgb = !upscale_linear && Vulkan::format_is_srgb(graph.get_physical_texture_resource(tex_out).get_format());

		const char *vert = use_custom ? "assets://shaders/upscale.vert" : "builtin://shaders/quad.vert";
		const char *frag = use_custom ? "assets://shaders/upscale.frag" : "builtin://shaders/post/lanczos2.frag";

		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, vert, frag,
		                                                {{ "TARGET_SRGB", srgb ? 1 : 0 }});
	});

	return true;
}

bool setup_before_post_chain_antialiasing(PostAAType type, RenderGraph &graph, TemporalJitter &jitter,
                                          float scaling_factor,
                                          const std::string &input, const std::string &input_depth,
                                          const std::string &output)
{
	TAAQuality taa_quality;
	switch (type)
	{
	case PostAAType::TAA_Low: taa_quality = TAAQuality::Low; break;
	case PostAAType::TAA_Medium: taa_quality = TAAQuality::Medium; break;
	case PostAAType::TAA_High: taa_quality = TAAQuality::High; break;
	case PostAAType::TAA_Ultra: taa_quality = TAAQuality::Ultra; break;
	case PostAAType::TAA_Extreme: taa_quality = TAAQuality::Extreme; break;
	case PostAAType::TAA_Nightmare: taa_quality = TAAQuality::Nightmare; break;
	default: return false;
	}

	setup_taa_resolve(graph, jitter, scaling_factor, input, input_depth, output, taa_quality);
	return true;
}

bool setup_after_post_chain_antialiasing(PostAAType type, RenderGraph &graph, TemporalJitter &jitter,
                                         float scaling_factor,
                                         const std::string &input, const std::string &input_depth,
                                         const std::string &output)
{
	switch (type)
	{
	case PostAAType::None:
		jitter.init(TemporalJitter::Type::None, vec2(0.0f));
		return false;

	case PostAAType::FXAA:
		setup_fxaa_postprocess(graph, input, output);
		return true;

	case PostAAType::FXAA_2Phase:
		setup_fxaa_2phase_postprocess(graph, jitter, input, input_depth, output);
		return true;

	case PostAAType::SMAA_Low:
		setup_smaa_postprocess(graph, jitter, scaling_factor, input, input_depth, output, SMAAPreset::Low);
		return true;

	case PostAAType::SMAA_Medium:
		setup_smaa_postprocess(graph, jitter, scaling_factor, input, input_depth, output, SMAAPreset::Medium);
		return true;

	case PostAAType::SMAA_High:
		setup_smaa_postprocess(graph, jitter, scaling_factor, input, input_depth, output, SMAAPreset::High);
		return true;

	case PostAAType::SMAA_Ultra:
		setup_smaa_postprocess(graph, jitter, scaling_factor, input, input_depth, output, SMAAPreset::Ultra);
		return true;

	case PostAAType::SMAA_Ultra_T2X:
		setup_smaa_postprocess(graph, jitter, scaling_factor, input, input_depth, output, SMAAPreset::Ultra_T2X);
		return true;

	default:
		return false;
	}
}

PostAAType string_to_post_antialiasing_type(const char *type)
{
	if (strcmp(type, "fxaa") == 0)
		return PostAAType::FXAA;
	else if (strcmp(type, "fxaa2phase") == 0)
		return PostAAType::FXAA_2Phase;
	else if (strcmp(type, "smaaLow") == 0)
		return PostAAType::SMAA_Low;
	else if (strcmp(type, "smaaMedium") == 0)
		return PostAAType::SMAA_Medium;
	else if (strcmp(type, "smaaHigh") == 0)
		return PostAAType::SMAA_High;
	else if (strcmp(type, "smaaUltra") == 0)
		return PostAAType::SMAA_Ultra;
	else if (strcmp(type, "smaaUltraT2X") == 0)
		return PostAAType::SMAA_Ultra_T2X;
	else if (strcmp(type, "taaLow") == 0)
		return PostAAType::TAA_Low;
	else if (strcmp(type, "taaMedium") == 0)
		return PostAAType::TAA_Medium;
	else if (strcmp(type, "taaHigh") == 0)
		return PostAAType::TAA_High;
	else if (strcmp(type, "taaUltra") == 0)
		return PostAAType::TAA_Ultra;
	else if (strcmp(type, "taaExtreme") == 0)
		return PostAAType::TAA_Extreme;
	else if (strcmp(type, "taaNightmare") == 0)
		return PostAAType::TAA_Nightmare;
	else if (strcmp(type, "none") == 0)
		return PostAAType::None;
	else
	{
		LOGE("Unrecognized AA type: %s\n", type);
		return PostAAType::None;
	}
}
}
