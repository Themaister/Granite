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

#include "render_graph.hpp"
#include "render_context.hpp"
#include "spd.hpp"
#include "command_buffer.hpp"
#include "common_renderer_data.hpp"
#include "lights/clusterer.hpp"
#include "lights/volumetric_diffuse.hpp"
#include "ssr.hpp"

namespace Granite
{
namespace blue
{
// Blue Noise Sampler by Eric Heitz. Returns a value in the range [0, 1].
#include "utils/blue/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.hpp"
}

struct UBO
{
	alignas(16) mat4 view_projection;
	alignas(16) mat4 inv_view_projection;
	alignas(8) vec2 float_resolution;
	alignas(8) vec2 inv_resolution;
	alignas(8) uvec2 resolution;
	alignas(4) uint32_t max_lod;
	alignas(4) uint32_t frame;
	alignas(16) vec3 camera_position;
	alignas(4) uint resolution_1d;
};

static void fill_ubo(UBO &ubo, const RenderContext &context, const Vulkan::ImageView &output_view,
                     const Vulkan::ImageView *depth_view,
                     unsigned frame)
{
	ubo.view_projection = context.get_render_parameters().view_projection;
	ubo.inv_view_projection = context.get_render_parameters().inv_view_projection;
	vec2 float_resolution(float(output_view.get_view_width()), float(output_view.get_view_height()));
	ubo.float_resolution = float_resolution;
	ubo.inv_resolution = 1.0f / float_resolution;
	ubo.resolution = uvec2(output_view.get_view_width(), output_view.get_view_height());
	ubo.camera_position = context.get_render_parameters().camera_position;
	ubo.max_lod = depth_view ? depth_view->get_create_info().levels - 1 : 0;
	ubo.frame = frame;
	ubo.resolution_1d = output_view.get_view_width() * output_view.get_view_height();
}

struct SSRState : RenderPassInterface
{
	void build_render_pass(Vulkan::CommandBuffer &cmd) override
	{
		Renderer::bind_lighting_parameters(cmd, *context);

		cmd.set_texture(2, 0, *depth_view);
		cmd.set_texture(2, 1, *base_color_view);
		cmd.set_texture(2, 2, *normal_view);
		cmd.set_texture(2, 3, *pbr_view);
		cmd.set_texture(2, 4, *light_view);
		cmd.set_texture(2, 5, dither_lut->get_view());
		cmd.set_storage_texture(2, 6, *output_view);
		cmd.set_storage_texture(2, 7, *ray_length_view);
		cmd.set_storage_texture(2, 8, *ray_confidence_view);
		cmd.set_storage_buffer(2, 9, *ray_counter_buffer);
		cmd.set_storage_buffer(2, 10, *ray_list_buffer);

		auto *ubo = cmd.allocate_typed_constant_data<UBO>(3, 0, 1);
		fill_ubo(*ubo, *context, *output_view, depth_view, frame);

		// ClassifyTiles.hlsl
		if (cmd.get_device().supports_subgroup_size_log2(true, 2, 6))
		{
			cmd.set_subgroup_size_log2(true, 2, 6);
			cmd.enable_subgroup_size_control(true);
		}
		cmd.set_program("builtin://shaders/post/ffx-sssr/classify.comp");
		cmd.dispatch((output_view->get_view_width() + 7) / 8, (output_view->get_view_height() + 7) / 8, 1);
		cmd.enable_subgroup_size_control(false);

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

		// PrepareIndirectArgs.hlsl
		cmd.set_program("builtin://shaders/post/ffx-sssr/build_indirect.comp");
		cmd.dispatch(1, 1, 1);

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
		            VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
		            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
		            VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
		            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

		// Intersect.hlsl
		cmd.set_program("builtin://shaders/post/ffx-sssr/trace_primary.comp");
		cmd.dispatch_indirect(*ray_counter_buffer, 0);

		if (auto *lighting = context->get_lighting_parameters())
		{
			if (lighting->volumetric_diffuse)
			{
				cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
				            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
				            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

				VkFormatProperties3 props3 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
				cmd.get_device().get_format_properties(output_view->get_format(), &props3);
				if (!(props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT))
					LOGW("Cannot read without format.\n");
				if (!(props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT))
					LOGW("Cannot write without format.\n");

				defines.clear();
				Renderer::add_subgroup_defines(cmd.get_device(), defines, VK_SHADER_STAGE_COMPUTE_BIT);
				if (cmd.get_device().supports_subgroup_size_log2(true, 2, 6))
				{
					defines.emplace_back("SUBGROUP_COMPUTE_FULL", 1);
					cmd.set_subgroup_size_log2(true, 2, 6);
					cmd.enable_subgroup_size_control(true);
				}

				cmd.set_program("builtin://shaders/post/ffx-sssr/trace_fallback.comp", defines);
				cmd.dispatch((output_view->get_view_width() + 7) / 8, (output_view->get_view_height() + 7) / 8, 1);
				cmd.enable_subgroup_size_control(false);
			}
		}
	}

	void enqueue_prepare_render_pass(RenderGraph &graph, TaskComposer &) override
	{
		output_view = &graph.get_physical_texture_resource(*output);
		ray_length_view = &graph.get_physical_texture_resource(*ray_length);
		ray_confidence_view = &graph.get_physical_texture_resource(*ray_confidence);
		depth_view = &graph.get_physical_texture_resource(*depth);
		normal_view = &graph.get_physical_texture_resource(*normal);
		pbr_view = &graph.get_physical_texture_resource(*pbr);
		light_view = &graph.get_physical_texture_resource(*light);
		base_color_view = &graph.get_physical_texture_resource(*base_color);
		ray_list_buffer = &graph.get_physical_buffer_resource(*ray_list);
		ray_counter_buffer = &graph.get_physical_buffer_resource(*ray_counter);
		frame = (frame + 1) % NumDitherIterations;
	}

	void setup(Vulkan::Device &device) override
	{
		constexpr int W = 128;
		constexpr int H = 128;
		auto info = Vulkan::ImageCreateInfo::immutable_2d_image(W, H, VK_FORMAT_R8G8_UNORM);
		info.layers = NumDitherIterations;
		info.levels = 1;

		const auto encode = [](float x, float y, float offset) -> uint16_t {
			x += offset;
			y += offset;
			x = fract(x);
			y = fract(y);
			auto ix = uint32_t(x * 255.0f + 0.5f);
			auto iy = uint32_t(y * 255.0f + 0.5f);
			return ix | (iy << 8);
		};

		constexpr float GOLDEN_RATIO = 1.61803398875f;

		// From https://github.com/GPUOpen-Effects/FidelityFX-SSSR/blob/master/sample/src/Shaders/PrepareBlueNoiseTexture.hlsl.
		std::vector<uint16_t> buffer(W * H * NumDitherIterations);
		for (int z = 0; z < int(NumDitherIterations); z++)
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++)
					buffer[z * W * H + y * W + x] = encode(
							blue::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 0),
							blue::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 1),
							GOLDEN_RATIO * float(z));

		Vulkan::ImageInitialData init[NumDitherIterations] = {};
		for (unsigned i = 0; i < NumDitherIterations; i++)
			init[i].data = buffer.data() + i * W * H;
		dither_lut = device.create_image(info, init);
		device.set_name(*dither_lut, "blue-noise-lut");
	}

	void setup_dependencies(RenderPass &self, RenderGraph &graph) override
	{
		auto *pass = graph.find_pass("probe-light");
		if (pass)
			self.add_proxy_input("probe-light-proxy", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}

	Vulkan::ImageHandle dither_lut;

	RenderTextureResource *output;
	RenderTextureResource *depth;
	RenderTextureResource *normal;
	RenderTextureResource *base_color;
	RenderTextureResource *pbr;
	RenderTextureResource *light;
	RenderTextureResource *ray_length;
	RenderTextureResource *ray_confidence;
	RenderBufferResource *ray_counter;
	RenderBufferResource *ray_list;
	const Vulkan::ImageView *output_view;
	const Vulkan::ImageView *depth_view;
	const Vulkan::ImageView *normal_view;
	const Vulkan::ImageView *pbr_view;
	const Vulkan::ImageView *light_view;
	const Vulkan::ImageView *ray_length_view;
	const Vulkan::ImageView *ray_confidence_view;
	const Vulkan::ImageView *base_color_view;
	const Vulkan::Buffer *ray_counter_buffer;
	const Vulkan::Buffer *ray_list_buffer;
	const RenderContext *context;
	unsigned frame = 0;
	std::vector<std::pair<std::string, int>> defines;
	enum { NumDitherIterations = 64 };
};

void setup_ssr_pass(RenderGraph &graph, const RenderContext &context,
                    const std::string &input_depth,
                    const std::string &input_base_color,
                    const std::string &input_normal,
                    const std::string &input_pbr, const std::string &input_light,
                    const std::string &output)
{
	setup_depth_hierarchy_pass(graph, input_depth, input_depth + "-hier");

	auto &pass = graph.add_pass(output + "-trace", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

	auto state = Util::make_handle<SSRState>();
	state->normal = &pass.add_texture_input(input_normal);
	state->pbr = &pass.add_texture_input(input_pbr);
	state->depth = &pass.add_texture_input(input_depth + "-hier");
	state->light = &pass.add_texture_input(input_light);
	state->base_color = &pass.add_texture_input(input_base_color);

	auto light_dim = graph.get_resource_dimensions(*state->light);

	AttachmentInfo att;
	att.size_class = SizeClass::InputRelative;
	att.size_relative_name = input_depth;
	att.format = light_dim.format;
	state->output = &pass.add_storage_texture_output(output + "-sssr", att);

	att.format = VK_FORMAT_R16_SFLOAT;
	state->ray_length = &pass.add_storage_texture_output(output + "-length", att);

	att.format = VK_FORMAT_R8_UNORM;
	state->ray_confidence = &pass.add_storage_texture_output(output + "-confidence", att);

	BufferInfo buf;
	buf.size = light_dim.width * light_dim.height * sizeof(uint32_t);
	state->ray_list = &pass.add_storage_output("ssr-ray-list", buf);

	buf.size = 4096;
	buf.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
	            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	state->ray_counter = &pass.add_storage_output("ssr-ray-counter", buf);

	state->context = &context;

	pass.set_render_pass_interface(std::move(state));

	// TODO: Figure out how to integrate FFX-DNSR denoiser.

	// Apply results with plain blending.
	auto &apply_pass = graph.add_pass(output, RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	auto &sssr_result = apply_pass.add_texture_input(output + "-sssr");
	AttachmentInfo output_attr;
	output_attr.size_class = SizeClass::InputRelative;
	output_attr.size_relative_name = input_light;
	output_attr.format = graph.get_resource_dimensions(sssr_result).format;
	apply_pass.add_color_output(output, output_attr, input_light);
	apply_pass.set_depth_stencil_input(input_depth);
	apply_pass.add_attachment_input(input_base_color);
	apply_pass.add_attachment_input(input_normal);
	apply_pass.add_attachment_input(input_pbr);
	apply_pass.add_attachment_input(input_depth);

	apply_pass.set_build_render_pass([&graph, &sssr_result, &context](Vulkan::CommandBuffer &cmd) {
		auto &res_view = graph.get_physical_texture_resource(sssr_result);
		cmd.set_texture(0, 0, res_view, Vulkan::StockSampler::NearestClamp);
		cmd.set_input_attachments(0, 1);

		auto *ubo = cmd.allocate_typed_constant_data<UBO>(3, 0, 1);
		fill_ubo(*ubo, context, res_view, nullptr, 0);

		cmd.set_texture(0, 5,
		                *cmd.get_device().get_resource_manager().get_image_view_blocking(GRANITE_COMMON_RENDERER_DATA()->brdf_tables),
		                Vulkan::StockSampler::LinearClamp);

		Vulkan::CommandBufferUtil::setup_fullscreen_quad(cmd,
		                                                 "builtin://shaders/post/ffx-sssr/apply.vert",
		                                                 "builtin://shaders/post/ffx-sssr/apply.frag",
		                                                 {},
		                                                 true, false,
		                                                 VK_COMPARE_OP_NOT_EQUAL);
		cmd.set_blend_enable(true);
		cmd.set_blend_factors(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE);
		cmd.set_blend_op(VK_BLEND_OP_ADD);
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd);
	});
}
}