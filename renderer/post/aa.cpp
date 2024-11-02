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

#include "aa.hpp"
#include "temporal.hpp"
#include "fxaa.hpp"
#include "smaa.hpp"
#include "muglm/muglm_impl.hpp"
#include "environment.hpp"
#include <string.h>

namespace Granite
{
// Rip impl out of the headers since there's a lot of weird warnings that I can't be arsed to work around.
static void FsrEasuCon(
		float *con0,
		float *con1,
		float *con2,
		float *con3,
		float inputViewportInPixelsX,
		float inputViewportInPixelsY,
		float inputSizeInPixelsX,
		float inputSizeInPixelsY,
		float outputSizeInPixelsX,
		float outputSizeInPixelsY)
{
	// Output integer position to a pixel position in viewport.
	con0[0] = inputViewportInPixelsX / outputSizeInPixelsX;
	con0[1] = inputViewportInPixelsY / outputSizeInPixelsY;
	con0[2] = 0.5f * inputViewportInPixelsX / outputSizeInPixelsX - 0.5f;
	con0[3] = 0.5f * inputViewportInPixelsY / outputSizeInPixelsY - 0.5f;
	con1[0] = 1.0f / inputSizeInPixelsX;
	con1[1] = 1.0f / inputSizeInPixelsY;
	con1[2] = 1.0f / inputSizeInPixelsX;
	con1[3] = -1.0f / inputSizeInPixelsY;
	con2[0] = -1.0f / inputSizeInPixelsX;
	con2[1] = 2.0f / inputSizeInPixelsY;
	con2[2] = 1.0f / inputSizeInPixelsX;
	con2[3] = 2.0f / inputSizeInPixelsY;
	con3[0] = 0.0f / inputSizeInPixelsX;
	con3[1] = 4.0f / inputSizeInPixelsY;
	con3[2] = con3[3] = 0.0f;
}

static void FsrRcasCon(float *con, float sharpness)
{
	sharpness = muglm::exp2(-sharpness);
	uint32_t half = floatToHalf(sharpness);
	con[0] = sharpness;
	uint32_t halves = half | (half << 16);
	memcpy(&con[1], &halves, sizeof(halves));
	con[2] = 0.0f;
	con[3] = 0.0f;
}

bool setup_after_post_chain_upscaling(RenderGraph &graph, const std::string &input, const std::string &output, bool use_sharpen)
{
	auto &upscale = graph.add_pass(output + "-scale", RenderGraph::get_default_post_graphics_queue());
	AttachmentInfo upscale_info;

	upscale_info.flags |= !use_sharpen ? ATTACHMENT_INFO_SUPPORTS_PREROTATE_BIT : 0;
	upscale_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	upscale_info.flags |= use_sharpen ? ATTACHMENT_INFO_UNORM_SRGB_ALIAS_BIT : 0;

	auto &upscale_tex_out = upscale.add_color_output(use_sharpen ? (output + "-scale") : output, upscale_info);
	auto &tex = upscale.add_texture_input(input);
	graph.get_texture_resource(input).get_attachment_info().flags |= ATTACHMENT_INFO_UNORM_SRGB_ALIAS_BIT;

	upscale.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto &view = graph.get_physical_texture_resource(tex);
		cmd.set_unorm_texture(0, 0, view);
		cmd.set_sampler(0, 0, Vulkan::StockSampler::NearestClamp);

		struct Constants
		{
			float params[4][4];
		} constants;

		struct Push
		{
			float width, height;
		} push;

		auto width = float(view.get_image().get_width());
		auto height = float(view.get_image().get_height());
		auto *params = cmd.allocate_typed_constant_data<Constants>(1, 0, 1);
		FsrEasuCon(constants.params[0], constants.params[1], constants.params[2], constants.params[3],
		           width, height, width, height, cmd.get_viewport().width, cmd.get_viewport().height);
		*params = constants;

		push.width = cmd.get_viewport().width;
		push.height = cmd.get_viewport().height;
		cmd.push_constants(&push, 0, sizeof(push));

		bool srgb = Vulkan::format_is_srgb(graph.get_physical_texture_resource(upscale_tex_out).get_format());
		const char *vert = "builtin://shaders/post/ffx-fsr/upscale.vert";
		const char *frag = "builtin://shaders/post/ffx-fsr/upscale.frag";

		bool fp16 = cmd.get_device().get_device_features().vk12_features.shaderFloat16;
		fp16 = Util::get_environment_bool("FIDELITYFX_FSR_FP16", fp16);

		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, vert, frag,
		                                                {{ "TARGET_SRGB", srgb ? 1 : 0 },
		                                                 {"FP16", fp16 ? 1 : 0 }});
	});

	if (use_sharpen)
	{
		AttachmentInfo sharpen_info;
		sharpen_info.flags |= ATTACHMENT_INFO_SUPPORTS_PREROTATE_BIT;

		auto &sharpen = graph.add_pass(output + "-sharpen", RenderGraph::get_default_post_graphics_queue());

		auto &sharpen_tex_out = sharpen.add_color_output(output, sharpen_info);
		auto &upscale_tex = sharpen.add_texture_input(output + "-scale");

		sharpen.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
			bool srgb = Vulkan::format_is_srgb(graph.get_physical_texture_resource(sharpen_tex_out).get_format());
			auto &view = graph.get_physical_texture_resource(upscale_tex);
			if (srgb)
				cmd.set_srgb_texture(0, 0, view);
			else
				cmd.set_unorm_texture(0, 0, view);
			cmd.set_sampler(0, 0, Vulkan::StockSampler::NearestClamp);

			struct Constants
			{
				float params[4];
				int32_t range[4];
			} constants;

			FsrRcasCon(constants.params, 0.5f);
			constants.range[0] = 0;
			constants.range[1] = 0;
			constants.range[2] = view.get_image().get_width() - 1;
			constants.range[3] = view.get_image().get_height() - 1;
			auto *params = cmd.allocate_typed_constant_data<Constants>(1, 0, 1);
			*params = constants;

			struct Push
			{
				float width, height;
			} push;
			push.width = cmd.get_viewport().width;
			push.height = cmd.get_viewport().height;
			cmd.push_constants(&push, 0, sizeof(push));

			const char *vert = "builtin://shaders/post/ffx-fsr/sharpen.vert";
			const char *frag = "builtin://shaders/post/ffx-fsr/sharpen.frag";
			Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, vert, frag);
		});
	}

	return true;
}

bool setup_before_post_chain_antialiasing(PostAAType type, RenderGraph &graph, TemporalJitter &jitter,
                                          const RenderContext &context,
                                          float scaling_factor,
                                          const std::string &input,
                                          const std::string &input_depth,
                                          const std::string &input_mv,
                                          const std::string &output)
{
	if (type == PostAAType::TAA_FSR2)
	{
		setup_fsr2_pass(graph, jitter, context, scaling_factor, input, input_depth, input_mv, output);
		return true;
	}
	else
	{
		TAAQuality taa_quality;
		switch (type)
		{
		case PostAAType::TAA_Low:
			taa_quality = TAAQuality::Low;
			break;
		case PostAAType::TAA_Medium:
			taa_quality = TAAQuality::Medium;
			break;
		case PostAAType::TAA_High:
			taa_quality = TAAQuality::High;
			break;
		default:
			return false;
		}

		setup_taa_resolve(graph, jitter, scaling_factor, input, input_depth, input_mv, output, taa_quality);
		return true;
	}
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
	if (!type)
		return PostAAType::None;

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
	else if (strcmp(type, "taaFSR2") == 0)
		return PostAAType::TAA_FSR2;
	else if (strcmp(type, "none") == 0)
		return PostAAType::None;
	else
	{
		LOGE("Unrecognized AA type: %s\n", type);
		return PostAAType::None;
	}
}
}
