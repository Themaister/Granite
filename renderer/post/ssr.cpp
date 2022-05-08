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
namespace blue
{
// Blue Noise Sampler by Eric Heitz. Returns a value in the range [0, 1].
#include "utils/blue/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.hpp"
}

struct SSRState : RenderPassInterface
{
	void build_render_pass(Vulkan::CommandBuffer &cmd) override
	{
		cmd.set_program("builtin://shaders/post/screenspace_trace.comp");
		cmd.set_texture(0, 0, *depth_view);
		cmd.set_texture(0, 1, *normal_view);
		cmd.set_texture(0, 2, *pbr_view);
		cmd.set_texture(0, 3, dither_lut->get_view());
		cmd.set_storage_texture(0, 4, *uv_view);

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
		};

		auto *ubo = cmd.allocate_typed_constant_data<UBO>(1, 0, 1);
		ubo->view_projection = context->get_render_parameters().view_projection;
		ubo->inv_view_projection = context->get_render_parameters().inv_view_projection;
		vec2 float_resolution(float(uv_view->get_view_width()), float(uv_view->get_view_height()));
		ubo->float_resolution = float_resolution;
		ubo->inv_resolution = 1.0f / float_resolution;
		ubo->resolution = uvec2(uv_view->get_view_width(), uv_view->get_view_height());
		ubo->camera_position = context->get_render_parameters().camera_position;
		ubo->max_lod = uv_view->get_create_info().levels - 1;
		ubo->frame = frame;

		cmd.dispatch((uv_view->get_view_width() + 7) / 8, (uv_view->get_view_height() + 7) / 8, 1);
	}

	void enqueue_prepare_render_pass(RenderGraph &graph, TaskComposer &) override
	{
		uv_view = &graph.get_physical_texture_resource(*uv);
		depth_view = &graph.get_physical_texture_resource(*depth);
		normal_view = &graph.get_physical_texture_resource(*normal);
		pbr_view = &graph.get_physical_texture_resource(*pbr);
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

	Vulkan::ImageHandle dither_lut;

	RenderTextureResource *uv;
	RenderTextureResource *depth;
	RenderTextureResource *normal;
	RenderTextureResource *pbr;
	const Vulkan::ImageView *uv_view;
	const Vulkan::ImageView *depth_view;
	const Vulkan::ImageView *normal_view;
	const Vulkan::ImageView *pbr_view;
	const RenderContext *context;
	unsigned frame = 0;
	enum { NumDitherIterations = 64 };
};

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

	auto state = Util::make_handle<SSRState>();
	state->normal = &pass.add_texture_input(input_normal);
	state->pbr = &pass.add_texture_input(input_pbr);
	state->depth = &pass.add_texture_input(input_depth + "-hier");
	state->uv = &pass.add_storage_texture_output(output, att);
	state->context = &context;

	pass.set_render_pass_interface(std::move(state));
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