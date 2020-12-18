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

#include "hdr.hpp"
#include "math.hpp"
#include "application_events.hpp"
#include "common_renderer_data.hpp"
#include "muglm/muglm_impl.hpp"

namespace Granite
{

static void luminance_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd,
                                        RenderTextureResource &input_res,
                                        RenderBufferResource &output_res)
{
	auto &input = pass.get_graph().get_physical_texture_resource(input_res);
	auto &output = pass.get_graph().get_physical_buffer_resource(output_res);

	cmd.set_storage_buffer(0, 0, output);
	cmd.set_texture(0, 1, input, Vulkan::StockSampler::LinearClamp);

	unsigned half_width = input.get_image().get_create_info().width / 2;
	unsigned half_height = input.get_image().get_create_info().height / 2;

	auto *program = cmd.get_device().get_shader_manager().register_compute("builtin://shaders/post/luminance.comp");
	auto *variant = program->register_variant({});
	cmd.set_program(variant->get_program());

	struct Registers
	{
		uvec2 size;
		float lerp;
		float minimum;
		float maximum;
	} push;
	push.size = uvec2(half_width, half_height);
	push.lerp = 1.0f - pow(0.5f, Global::common_renderer_data()->frame_tick.frame_time);
	push.minimum = -3.0f;
	push.maximum = 2.0f;
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(1, 1, 1);
}

static void luminance_build_compute(Vulkan::CommandBuffer &cmd, RenderGraph &graph,
                                    const RenderBufferResource &lum, const RenderTextureResource &d3)
{
	auto &input = graph.get_physical_texture_resource(d3);
	auto &output = graph.get_physical_buffer_resource(lum);

	cmd.set_storage_buffer(0, 0, output);
	cmd.set_texture(0, 1, input, Vulkan::StockSampler::LinearClamp);

	unsigned half_width = input.get_image().get_create_info().width / 2;
	unsigned half_height = input.get_image().get_create_info().height / 2;

	auto *program = cmd.get_device().get_shader_manager().register_compute("builtin://shaders/post/luminance.comp");
	auto *variant = program->register_variant({});
	cmd.set_program(variant->get_program());

	struct Registers
	{
		uvec2 size;
		float lerp;
		float minimum;
		float maximum;
	} push;
	push.size = uvec2(half_width, half_height);
	push.lerp = 1.0f - pow(0.5f, Global::common_renderer_data()->frame_tick.frame_time);
	push.minimum = -3.0f;
	push.maximum = 2.0f;
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(1, 1, 1);
}

static void bloom_threshold_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd,
                                              const RenderTextureResource &input_res,
                                              const RenderBufferResource *ubo_res)
{
	auto &input = pass.get_graph().get_physical_texture_resource(input_res);
	auto *ubo = ubo_res ? &pass.get_graph().get_physical_buffer_resource(*ubo_res) : nullptr;
	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);
	if (ubo)
		cmd.set_uniform_buffer(0, 1, *ubo);

	Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
	                                                "builtin://shaders/post/bloom_threshold.frag",
	                                                {{ "DYNAMIC_EXPOSURE", ubo ? 1 : 0 }});
}

static void bloom_threshold_build_compute(Vulkan::CommandBuffer &cmd, RenderGraph &graph,
                                          const RenderTextureResource &threshold,
                                          const RenderTextureResource &hdr,
                                          const RenderBufferResource *ubo_res)
{
	auto &output = graph.get_physical_texture_resource(threshold);
	auto &input = graph.get_physical_texture_resource(hdr);
	auto *ubo = ubo_res ? &graph.get_physical_buffer_resource(*ubo_res) : nullptr;

	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);
	if (ubo)
		cmd.set_uniform_buffer(0, 1, *ubo);
	cmd.set_storage_texture(0, 2, output);

	cmd.set_program(
			"builtin://shaders/post/bloom_threshold.comp",
			{{ "DYNAMIC_EXPOSURE", ubo ? 1 : 0 }});

	struct Registers
	{
		uvec2 threads;
		vec2 inv_resolution;
	} push;
	push.threads.x = output.get_image().get_width();
	push.threads.y = output.get_image().get_height();
	push.inv_resolution.x = 1.0f / float(push.threads.x);
	push.inv_resolution.y = 1.0f / float(push.threads.y);
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((push.threads.x + 7) / 8, (push.threads.y + 7) / 8, 1);
}

static void bloom_downsample_build_compute(Vulkan::CommandBuffer &cmd, RenderGraph &graph,
                                           const RenderTextureResource &output_res, const RenderTextureResource &input_res,
                                           const RenderTextureResource *feedback)
{
	auto &output = graph.get_physical_texture_resource(output_res);
	auto &input = graph.get_physical_texture_resource(input_res);

	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);
	cmd.set_storage_texture(0, 1, output);

	if (feedback)
	{
		auto *history = graph.get_physical_history_texture_resource(*feedback);
		if (history)
			cmd.set_texture(0, 2, *history, Vulkan::StockSampler::NearestClamp);
		else
			feedback = nullptr;
	}

	auto *program = cmd.get_device().get_shader_manager().register_compute("builtin://shaders/post/bloom_downsample.comp");
	auto *variant = program->register_variant({{ "FEEDBACK", feedback ? 1 : 0 }});
	cmd.set_program(variant->get_program());

	struct Registers
	{
		uvec2 threads;
		vec2 inv_output_resolution;
		vec2 inv_input_resolution;
		float lerp;
	} push;
	push.threads.x = output.get_image().get_width();
	push.threads.y = output.get_image().get_height();
	push.inv_output_resolution.x = 1.0f / float(push.threads.x);
	push.inv_output_resolution.y = 1.0f / float(push.threads.y);
	push.inv_input_resolution.x = 1.0f / float(input.get_image().get_width());
	push.inv_input_resolution.y = 1.0f / float(input.get_image().get_height());
	float lerp = 1.0f - pow(0.001f, Global::common_renderer_data()->frame_tick.frame_time);
	push.lerp = lerp;

	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((push.threads.x + 7) / 8, (push.threads.y + 7) / 8, 1);
}

static void bloom_upsample_build_compute(Vulkan::CommandBuffer &cmd, RenderGraph &graph,
                                         const RenderTextureResource &output_res, const RenderTextureResource &input_res)
{
	auto &output = graph.get_physical_texture_resource(output_res);
	auto &input = graph.get_physical_texture_resource(input_res);

	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);
	cmd.set_storage_texture(0, 1, output);

	auto *program = cmd.get_device().get_shader_manager().register_compute("builtin://shaders/post/bloom_upsample.comp");
	auto *variant = program->register_variant({});
	cmd.set_program(variant->get_program());

	struct Registers
	{
		uvec2 threads;
		vec2 inv_output_resolution;
		vec2 inv_input_resolution;
	} push;
	push.threads.x = output.get_image().get_width();
	push.threads.y = output.get_image().get_height();
	push.inv_output_resolution.x = 1.0f / float(push.threads.x);
	push.inv_output_resolution.y = 1.0f / float(push.threads.y);
	push.inv_input_resolution.x = 1.0f / float(input.get_image().get_width());
	push.inv_input_resolution.y = 1.0f / float(input.get_image().get_height());
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch((push.threads.x + 7) / 8, (push.threads.y + 7) / 8, 1);
}

static void bloom_downsample_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd,
                                               RenderTextureResource &input_res,
                                               RenderTextureResource *feedback_res,
                                               bool feedback)
{
	auto &input = pass.get_graph().get_physical_texture_resource(input_res);
	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);

	if (feedback)
	{
		auto *feedback_texture = pass.get_graph().get_physical_history_texture_resource(*feedback_res);

		if (feedback_texture)
		{
			struct Push
			{
				vec2 inv_size;
				float lerp;
			} push;
			push.inv_size = vec2(1.0f / input.get_image().get_create_info().width,
			                     1.0f / input.get_image().get_create_info().height);

			float lerp = 1.0f - pow(0.001f, Global::common_renderer_data()->frame_tick.frame_time);
			push.lerp = lerp;
			cmd.push_constants(&push, 0, sizeof(push));

			cmd.set_texture(0, 1, *feedback_texture, Vulkan::StockSampler::NearestClamp);
			Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
			                                                "builtin://shaders/quad.vert",
			                                                "builtin://shaders/post/bloom_downsample.frag",
			                                                {{"FEEDBACK", 1}});
		}
		else
		{
			vec2 inv_size = vec2(1.0f / input.get_image().get_create_info().width,
			                     1.0f / input.get_image().get_create_info().height);
			cmd.push_constants(&inv_size, 0, sizeof(inv_size));
			Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
			                                                "builtin://shaders/quad.vert",
			                                                "builtin://shaders/post/bloom_downsample.frag");
		}
	}
	else
	{
		vec2 inv_size = vec2(1.0f / input.get_image().get_create_info().width,
		                     1.0f / input.get_image().get_create_info().height);
		cmd.push_constants(&inv_size, 0, sizeof(inv_size));
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
		                                                "builtin://shaders/quad.vert",
		                                                "builtin://shaders/post/bloom_downsample.frag");
	}
}

static void bloom_upsample_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd,
                                             RenderTextureResource &input_res)
{
	auto &input = pass.get_graph().get_physical_texture_resource(input_res);
	vec2 inv_size = vec2(1.0f / input.get_image().get_create_info().width, 1.0f / input.get_image().get_create_info().height);
	cmd.push_constants(&inv_size, 0, sizeof(inv_size));
	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);
	Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
	                                                "builtin://shaders/post/bloom_upsample.frag");
}

static void tonemap_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd,
                                      const RenderTextureResource &hdr_res,
                                      const RenderTextureResource &bloom_res,
                                      const RenderBufferResource *ubo_res,
                                      const HDRDynamicExposureInterface *iface)
{
	auto &hdr = pass.get_graph().get_physical_texture_resource(hdr_res);
	auto &bloom = pass.get_graph().get_physical_texture_resource(bloom_res);
	auto *ubo = ubo_res ? &pass.get_graph().get_physical_buffer_resource(*ubo_res) : nullptr;
	cmd.set_texture(0, 0, hdr, Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(0, 1, bloom, Vulkan::StockSampler::LinearClamp);
	if (ubo)
		cmd.set_uniform_buffer(0, 2, *ubo);

	struct Push
	{
		float dynamic_exposure;
	} push;
	push.dynamic_exposure = iface ? iface->get_exposure() : 1.0f;
	cmd.push_constants(&push, 0, sizeof(push));
	Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
	                                                "builtin://shaders/post/tonemap.frag",
	                                                {{ "DYNAMIC_EXPOSURE", ubo ? 1 : 0 }});
}

void setup_hdr_postprocess_compute(RenderGraph &graph, const std::string &input, const std::string &output,
                                   const HDROptions &options,
                                   const HDRDynamicExposureInterface *iface)
{
	BufferInfo buffer_info;
	buffer_info.size = 3 * sizeof(float);
	buffer_info.persistent = true;
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	AttachmentInfo downsample_info;
	downsample_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	downsample_info.size_x = 0.5f;
	downsample_info.size_y = 0.5f;
	downsample_info.size_class = SizeClass::InputRelative;
	downsample_info.size_relative_name = input;
	downsample_info.aux_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	auto downsample_info0 = downsample_info;
	auto downsample_info1 = downsample_info;
	auto downsample_info2 = downsample_info;
	auto downsample_info3 = downsample_info;
	downsample_info0.size_x = 0.25f;
	downsample_info0.size_y = 0.25f;
	downsample_info1.size_x = 0.125f;
	downsample_info1.size_y = 0.125f;
	downsample_info2.size_x = 0.0625f;
	downsample_info2.size_y = 0.0625f;
	downsample_info3.size_x = 0.03125f;
	downsample_info3.size_y = 0.03125f;

	auto &bloom_pass = graph.add_pass("bloom-compute", RenderGraph::get_default_compute_queue());
	// Workaround a cache invalidation driver bug by not aliasing.
	auto &t = bloom_pass.add_storage_texture_output("threshold", downsample_info);
	auto &d0 = bloom_pass.add_storage_texture_output("downsample-0", downsample_info0);
	auto &u0 = bloom_pass.add_storage_texture_output("upsample-0", downsample_info0);
	auto &d1 = bloom_pass.add_storage_texture_output("downsample-1", downsample_info1);
	auto &u1 = bloom_pass.add_storage_texture_output("upsample-1", downsample_info1);
	auto &d2 = bloom_pass.add_storage_texture_output("downsample-2", downsample_info2);
	auto &u2 = bloom_pass.add_storage_texture_output("upsample-2", downsample_info2);
	auto &d3 = bloom_pass.add_storage_texture_output("downsample-3", downsample_info3);

	const RenderBufferResource *lum = nullptr;
	if (options.dynamic_exposure)
		lum = &bloom_pass.add_storage_output("average-luminance", buffer_info);

	auto &hdr = bloom_pass.add_texture_input(input);
	bloom_pass.add_history_input("downsample-3");
	bloom_pass.set_build_render_pass([&, ubo = lum](Vulkan::CommandBuffer &cmd) {
		bloom_threshold_build_compute(cmd, graph, t, hdr, ubo);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		bloom_downsample_build_compute(cmd, graph, d0, t, nullptr);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		bloom_downsample_build_compute(cmd, graph, d1, d0, nullptr);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		bloom_downsample_build_compute(cmd, graph, d2, d1, nullptr);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		bloom_downsample_build_compute(cmd, graph, d3, d2, &d3);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		if (ubo)
			luminance_build_compute(cmd, graph, *ubo, d3);
		bloom_upsample_build_compute(cmd, graph, u2, d3);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT);
		bloom_upsample_build_compute(cmd, graph, u1, u2);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		bloom_upsample_build_compute(cmd, graph, u0, u1);
	});

	{
		AttachmentInfo tonemap_info;
		tonemap_info.supports_prerotate = true;
		tonemap_info.size_class = SizeClass::InputRelative;
		tonemap_info.size_relative_name = input;
		auto &tonemap = graph.add_pass("tonemap", RenderGraph::get_default_post_graphics_queue());
		tonemap.add_color_output(output, tonemap_info);
		auto &hdr_res = tonemap.add_texture_input(input);
		auto &bloom_res = tonemap.add_texture_input("upsample-0");

		const RenderBufferResource *ubo_res = nullptr;
		if (options.dynamic_exposure)
			ubo_res = &tonemap.add_uniform_input("average-luminance");

		tonemap.set_build_render_pass([&, iface = iface, ubo = ubo_res](Vulkan::CommandBuffer &cmd)
		                              {
			                              tonemap_build_render_pass(tonemap, cmd, hdr_res, bloom_res, ubo, iface);
		                              });
	}
}

void setup_hdr_postprocess(RenderGraph &graph, const std::string &input, const std::string &output,
                           const HDROptions &options,
                           const HDRDynamicExposureInterface *iface)
{
	BufferInfo buffer_info;
	buffer_info.size = 3 * sizeof(float);
	buffer_info.persistent = true;
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	if (options.dynamic_exposure)
	{
		auto &lum = graph.get_buffer_resource("average-luminance");
		lum.set_buffer_info(buffer_info);

		{
			auto &adapt_pass = graph.add_pass("adapt-luminance", RenderGraph::get_default_compute_queue());
			auto &output_res = adapt_pass.add_storage_output("average-luminance-updated", buffer_info,
			                                                 "average-luminance");
			auto &input_res = adapt_pass.add_texture_input("bloom-downsample-3");
			adapt_pass.set_build_render_pass([&](Vulkan::CommandBuffer &cmd)
			                                 {
				                                 luminance_build_render_pass(adapt_pass, cmd, input_res, output_res);
			                                 });
		}
	}

	{
		auto &threshold = graph.add_pass("bloom-threshold", RenderGraph::get_default_post_graphics_queue());
		AttachmentInfo threshold_info;
		threshold_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		threshold_info.size_x = 0.5f;
		threshold_info.size_y = 0.5f;
		threshold_info.size_class = SizeClass::InputRelative;
		threshold_info.size_relative_name = input;
		threshold.add_color_output("threshold", threshold_info);
		auto &input_res = threshold.add_texture_input(input);

		const RenderBufferResource *ubo_res = nullptr;
		if (options.dynamic_exposure)
			ubo_res = &threshold.add_uniform_input("average-luminance");

		threshold.set_build_render_pass([&, ubo = ubo_res](Vulkan::CommandBuffer &cmd)
		                                {
			                                bloom_threshold_build_render_pass(threshold, cmd, input_res, ubo);
		                                });
	}

	AttachmentInfo blur_info;
	{
		blur_info.size_x = 0.25f;
		blur_info.size_y = 0.25f;
		blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		blur_info.size_class = SizeClass::InputRelative;
		blur_info.size_relative_name = input;
		auto &blur0 = graph.add_pass("bloom-downsample-0", RenderGraph::get_default_post_graphics_queue());
		blur0.add_color_output("bloom-downsample-0", blur_info);
		auto &input_res = blur0.add_texture_input("threshold");
		blur0.set_build_render_pass([&](Vulkan::CommandBuffer &cmd)
		                            {
			                            bloom_downsample_build_render_pass(blur0, cmd, input_res, nullptr, false);
		                            });
	}

	{
		blur_info.size_x = 0.125f;
		blur_info.size_y = 0.125f;
		blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		auto &blur1 = graph.add_pass("bloom-downsample-1", RenderGraph::get_default_post_graphics_queue());
		blur1.add_color_output("bloom-downsample-1", blur_info);
		auto &input_res = blur1.add_texture_input("bloom-downsample-0");
		blur1.set_build_render_pass([&](Vulkan::CommandBuffer &cmd)
		                            {
			                            bloom_downsample_build_render_pass(blur1, cmd, input_res, nullptr, false);
		                            });
	}

	{
		blur_info.size_x = 0.0625f;
		blur_info.size_y = 0.0625f;
		blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		auto &blur2 = graph.add_pass("bloom-downsample-2", RenderGraph::get_default_post_graphics_queue());
		blur2.add_color_output("bloom-downsample-2", blur_info);
		auto &input_res = blur2.add_texture_input("bloom-downsample-1");
		blur2.set_build_render_pass([&](Vulkan::CommandBuffer &cmd)
		                            {
			                            bloom_downsample_build_render_pass(blur2, cmd, input_res, nullptr, false);
		                            });
	}

	{
		blur_info.size_x = 0.03125f;
		blur_info.size_y = 0.03125f;
		blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		auto &blur3 = graph.add_pass("bloom-downsample-3", RenderGraph::get_default_post_graphics_queue());
		blur3.add_color_output("bloom-downsample-3", blur_info);
		auto &input_res = blur3.add_texture_input("bloom-downsample-2");
		auto &feedback = blur3.add_history_input("bloom-downsample-3");
		blur3.set_build_render_pass([&](Vulkan::CommandBuffer &cmd)
		                            {
			                            bloom_downsample_build_render_pass(blur3, cmd, input_res, &feedback, true);
		                            });
	}

	{
		blur_info.size_x = 0.0625f;
		blur_info.size_y = 0.0625f;
		blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		auto &blur4 = graph.add_pass("bloom-upsample-0", RenderGraph::get_default_post_graphics_queue());
		blur4.add_color_output("bloom-upsample-0", blur_info);
		auto &input_res = blur4.add_texture_input("bloom-downsample-3");
		blur4.set_build_render_pass([&](Vulkan::CommandBuffer &cmd)
		                            {
			                            bloom_upsample_build_render_pass(blur4, cmd, input_res);
		                            });
	}

	{
		blur_info.size_x = 0.125f;
		blur_info.size_y = 0.125f;
		blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		auto &blur5 = graph.add_pass("bloom-upsample-1", RenderGraph::get_default_post_graphics_queue());
		blur5.add_color_output("bloom-upsample-1", blur_info);
		auto &input_res = blur5.add_texture_input("bloom-upsample-0");
		blur5.set_build_render_pass([&](Vulkan::CommandBuffer &cmd)
		                            {
			                            bloom_upsample_build_render_pass(blur5, cmd, input_res);
		                            });
	}

	{
		blur_info.size_x = 0.25f;
		blur_info.size_y = 0.25f;
		blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		auto &blur6 = graph.add_pass("bloom-upsample-2", RenderGraph::get_default_post_graphics_queue());
		blur6.add_color_output("bloom-upsample-2", blur_info);
		auto &input_res = blur6.add_texture_input("bloom-upsample-1");
		blur6.set_build_render_pass([&](Vulkan::CommandBuffer &cmd)
		                            {
			                            bloom_upsample_build_render_pass(blur6, cmd, input_res);
		                            });
	}

	{
		AttachmentInfo tonemap_info;
		tonemap_info.size_class = SizeClass::InputRelative;
		tonemap_info.size_relative_name = input;
		auto &tonemap = graph.add_pass("tonemap", RenderGraph::get_default_post_graphics_queue());
		tonemap.add_color_output(output, tonemap_info);
		auto &hdr_res = tonemap.add_texture_input(input);
		auto &bloom_res = tonemap.add_texture_input("bloom-upsample-2");
		const RenderBufferResource *ubo_res = nullptr;
		if (options.dynamic_exposure)
			ubo_res = &tonemap.add_uniform_input("average-luminance-updated");

		tonemap.set_build_render_pass([&, iface = iface, ubo = ubo_res](Vulkan::CommandBuffer &cmd)
		                              {
			                              tonemap_build_render_pass(tonemap, cmd, hdr_res, bloom_res, ubo, iface);
		                              });
	}
}
}
