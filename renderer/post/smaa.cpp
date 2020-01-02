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

#include "smaa.hpp"
#include "math.hpp"
#include "temporal.hpp"
#include "muglm/matrix_helper.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>

using namespace std;

namespace Granite
{
void setup_smaa_postprocess(RenderGraph &graph, TemporalJitter &jitter,
                            const string &input, const string &input_depth,
                            const string &output, SMAAPreset preset)
{
	bool t2x_enable = preset == SMAAPreset::Ultra_T2X;
	unsigned smaa_quality = 0;

	switch (preset)
	{
	case SMAAPreset::Low:
		smaa_quality = 0;
		break;

	case SMAAPreset::Medium:
		smaa_quality = 1;
		break;

	case SMAAPreset::High:
		smaa_quality = 2;
		break;

	case SMAAPreset::Ultra:
	case SMAAPreset::Ultra_T2X:
		smaa_quality = 3;
		break;
	}

	if (t2x_enable)
	{
		jitter.init(TemporalJitter::Type::SMAA_T2X,
		            vec2(graph.get_backbuffer_dimensions().width, graph.get_backbuffer_dimensions().height));
	}
	else
		jitter.init(TemporalJitter::Type::None, vec2(1.0f));

	const bool masked_edge = true;
	graph.get_texture_resource(input).get_attachment_info().unorm_srgb_alias = true;

	AttachmentInfo smaa_edge_output;
	smaa_edge_output.size_class = SizeClass::InputRelative;
	smaa_edge_output.size_relative_name = input;
	smaa_edge_output.format = VK_FORMAT_R8G8_UNORM;

	AttachmentInfo smaa_weight_output;
	smaa_weight_output.size_class = SizeClass::InputRelative;
	smaa_weight_output.size_relative_name = input;
	smaa_weight_output.format = VK_FORMAT_R8G8B8A8_UNORM;

	AttachmentInfo smaa_output;
	smaa_output.size_class = SizeClass::InputRelative;
	smaa_output.size_relative_name = input;

	AttachmentInfo smaa_depth;
	smaa_depth.size_class = SizeClass::InputRelative;
	smaa_depth.size_relative_name = input;
	smaa_depth.format = VK_FORMAT_D16_UNORM;

	auto &smaa_edge = graph.add_pass("smaa-edge", RenderGraph::get_default_post_graphics_queue());
	auto &smaa_weight = graph.add_pass("smaa-weights", RenderGraph::get_default_post_graphics_queue());
	auto &smaa_blend = graph.add_pass("smaa-blend", RenderGraph::get_default_post_graphics_queue());

	smaa_edge.add_color_output("smaa-edge", smaa_edge_output);
	auto &edge_input_res = smaa_edge.add_texture_input(input);
	if (masked_edge)
	{
		smaa_edge.set_depth_stencil_output("smaa-mask", smaa_depth);
		smaa_edge.set_get_clear_depth_stencil([&](VkClearDepthStencilValue *value) {
			if (value)
			{
				value->depth = 1.0f;
				value->stencil = 0;
			}
			return true;
		});
	}

	smaa_weight.add_color_output("smaa-weights", smaa_weight_output);
	auto &weight_input_res = smaa_weight.add_texture_input("smaa-edge");

	if (masked_edge)
		smaa_weight.set_depth_stencil_input("smaa-mask");

	smaa_blend.add_color_output(t2x_enable ? string("smaa-sample") : output, smaa_output);
	auto &blend_input_res = smaa_blend.add_texture_input(input);
	auto &blend_weight_res = smaa_blend.add_texture_input("smaa-weights");

	smaa_edge.set_build_render_pass([&, edge = masked_edge, q = smaa_quality](Vulkan::CommandBuffer &cmd) {
		auto &input_image = graph.get_physical_texture_resource(edge_input_res);
		cmd.set_unorm_texture(0, 0, input_image);
		cmd.set_sampler(0, 0, Vulkan::StockSampler::LinearClamp);
		vec4 rt_metrics(1.0f / input_image.get_image().get_create_info().width,
		                1.0f / input_image.get_image().get_create_info().height,
		                float(input_image.get_image().get_create_info().width),
		                float(input_image.get_image().get_create_info().height));
		cmd.push_constants(&rt_metrics, 0, sizeof(rt_metrics));

		Vulkan::CommandBufferUtil::draw_fullscreen_quad_depth(cmd,
		                                                      "builtin://shaders/post/smaa_edge_detection.vert",
		                                                      "builtin://shaders/post/smaa_edge_detection.frag",
		                                                      edge, edge, VK_COMPARE_OP_ALWAYS,
		                                                      {{"SMAA_QUALITY", q}});
	});

	smaa_edge.set_get_clear_color([](unsigned, VkClearColorValue *value) {
		if (value)
			memset(value, 0, sizeof(*value));
		return true;
	});

	smaa_weight.set_build_render_pass([&, edge = masked_edge, q = smaa_quality](Vulkan::CommandBuffer &cmd) {
		auto &input_image = graph.get_physical_texture_resource(weight_input_res);
		cmd.set_texture(0, 0, input_image, Vulkan::StockSampler::LinearClamp);
		cmd.set_texture(0, 1,
		                cmd.get_device().get_texture_manager().request_texture("builtin://textures/smaa/area.gtx")->get_image()->get_view(),
		                Vulkan::StockSampler::LinearClamp);
		cmd.set_texture(0, 2,
		                cmd.get_device().get_texture_manager().request_texture("builtin://textures/smaa/search.gtx")->get_image()->get_view(),
		                Vulkan::StockSampler::LinearClamp);
		vec4 rt_metrics(1.0f / input_image.get_image().get_create_info().width,
		                1.0f / input_image.get_image().get_create_info().height,
		                float(input_image.get_image().get_create_info().width),
		                float(input_image.get_image().get_create_info().height));
		cmd.push_constants(&rt_metrics, 0, sizeof(rt_metrics));

		int subpixel_mode = 0;
		if (jitter.get_jitter_type() == TemporalJitter::Type::SMAA_T2X)
			subpixel_mode = 1 + jitter.get_jitter_phase();

		Vulkan::CommandBufferUtil::draw_fullscreen_quad_depth(cmd,
		                                                      "builtin://shaders/post/smaa_blend_weight.vert",
		                                                      "builtin://shaders/post/smaa_blend_weight.frag",
		                                                      edge, false, VK_COMPARE_OP_EQUAL,
		                                                      {
				                                                      {"SMAA_SUBPIXEL_MODE", subpixel_mode},
				                                                      {"SMAA_QUALITY",       q}
		                                                      });
	});

	smaa_weight.set_get_clear_color([](unsigned, VkClearColorValue *value) {
		if (value)
			memset(value, 0, sizeof(*value));
		return true;
	});

	smaa_blend.set_build_render_pass([&, q = smaa_quality](Vulkan::CommandBuffer &cmd) {
		auto &input_image = graph.get_physical_texture_resource(blend_input_res);
		auto &blend_image = graph.get_physical_texture_resource(blend_weight_res);
		cmd.set_texture(0, 0, input_image, Vulkan::StockSampler::LinearClamp);
		cmd.set_texture(0, 1, blend_image, Vulkan::StockSampler::LinearClamp);
		vec4 rt_metrics(1.0f / input_image.get_image().get_create_info().width,
		                1.0f / input_image.get_image().get_create_info().height,
		                float(input_image.get_image().get_create_info().width),
		                float(input_image.get_image().get_create_info().height));

		cmd.push_constants(&rt_metrics, 0, sizeof(rt_metrics));

		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
		                                                "builtin://shaders/post/smaa_neighbor_blend.vert",
		                                                "builtin://shaders/post/smaa_neighbor_blend.frag",
		                                                {{"SMAA_QUALITY", q}});
	});

	if (t2x_enable)
	{
		auto &smaa_resolve = graph.add_pass("smaa-t2x-resolve", RenderGraph::get_default_post_graphics_queue());
		smaa_resolve.add_color_output(output, smaa_output);
		auto &input_res = smaa_resolve.add_texture_input("smaa-sample");
		auto &depth_res = smaa_resolve.add_texture_input(input_depth);
		auto &history_res = smaa_resolve.add_history_input("smaa-sample");

		AttachmentInfo variance;
		variance.size_relative_name = input;
		variance.format = VK_FORMAT_R8_UNORM;
		variance.size_class = SizeClass::InputRelative;
		smaa_resolve.add_color_output("smaa-variance", variance);
		smaa_resolve.add_history_input("smaa-variance");

		smaa_resolve.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
			auto &current = graph.get_physical_texture_resource(input_res);
			auto *prev = graph.get_physical_history_texture_resource(history_res);
			auto &depth = graph.get_physical_texture_resource(depth_res);

			cmd.set_texture(0, 0, current, Vulkan::StockSampler::NearestClamp);
			if (prev)
			{
				cmd.set_texture(0, 1, depth, Vulkan::StockSampler::NearestClamp);
				cmd.set_texture(0, 2, *prev, Vulkan::StockSampler::LinearClamp);
				cmd.set_texture(0, 4,
				                *graph.get_physical_history_texture_resource(smaa_resolve.get_history_inputs()[1]->get_physical_index()),
				                Vulkan::StockSampler::NearestClamp);
			}

			struct Push
			{
				mat4 reproj;
				vec2 inv_resolution_seed;
			};
			Push push;

			push.reproj =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				jitter.get_history_view_proj(1) *
				jitter.get_history_inv_view_proj(0);

			push.inv_resolution_seed = vec2(1.0f / current.get_image().get_create_info().width,
			                                1.0f / current.get_image().get_create_info().height);

			cmd.push_constants(&push, 0, sizeof(push));
			Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
			                                                "builtin://shaders/quad.vert",
			                                                "builtin://shaders/post/smaa_t2x_resolve.frag",
			                                                {{"REPROJECTION_HISTORY", prev ? 1 : 0}});
		});
	}
}
}
