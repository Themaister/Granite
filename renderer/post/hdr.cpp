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

#include "hdr.hpp"
#include "math.hpp"
#include "application.hpp"

namespace Granite
{
struct FrameEvent : EventHandler
{
	FrameEvent()
	{
		EVENT_MANAGER_REGISTER(FrameEvent, on_frame_time, FrameTickEvent);
	}

	bool on_frame_time(const Event &e)
	{
		frame_time = float(e.as<FrameTickEvent>().get_frame_time());
		return true;
	}

	float frame_time = 0.0f;
};

static FrameEvent timer;

static void luminance_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd)
{
	auto &input = pass.get_graph().get_physical_texture_resource(pass.get_texture_inputs()[0]->get_physical_index());
	auto &output = pass.get_graph().get_physical_buffer_resource(pass.get_storage_outputs()[0]->get_physical_index());

	cmd.set_storage_buffer(0, 0, output);
	cmd.set_texture(0, 1, input, Vulkan::StockSampler::LinearClamp);

	unsigned half_width = input.get_image().get_create_info().width / 2;
	unsigned half_height = input.get_image().get_create_info().height / 2;

	auto *program = cmd.get_device().get_shader_manager().register_compute("assets://shaders/post/luminance.comp");
	unsigned variant = program->register_variant({});
	cmd.set_program(*program->get_program(variant));

	struct Registers
	{
		uvec2 size;
		float lerp;
	} push;
	push.size = uvec2(half_width, half_height);
	push.lerp = 1.0f - pow(0.5f, timer.frame_time);
	cmd.push_constants(&push, 0, sizeof(push));
	cmd.dispatch(1, 1, 1);
}

static void bloom_threshold_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd)
{
	auto &input = pass.get_graph().get_physical_texture_resource(pass.get_texture_inputs()[0]->get_physical_index());
	auto &ubo = pass.get_graph().get_physical_buffer_resource(pass.get_uniform_inputs()[0]->get_physical_index());
	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);
	cmd.set_uniform_buffer(0, 1, ubo);
	Vulkan::CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/post/bloom_threshold.frag");
}

static void bloom_downsample_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd, bool feedback)
{
	auto &input = pass.get_graph().get_physical_texture_resource(pass.get_texture_inputs()[0]->get_physical_index());
	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);

	if (feedback)
	{
		auto *feedback_texture = pass.get_graph().get_physical_history_texture_resource(
			pass.get_history_inputs()[0]->get_physical_index());

		if (feedback_texture)
		{
			struct Push
			{
				vec2 inv_size;
				float lerp;
			} push;
			push.inv_size = vec2(1.0f / input.get_image().get_create_info().width,
			                     1.0f / input.get_image().get_create_info().height);

			float lerp = 1.0f - pow(0.001f, timer.frame_time);
			push.lerp = lerp;
			cmd.push_constants(&push, 0, sizeof(push));

			cmd.set_texture(0, 1, *feedback_texture, Vulkan::StockSampler::NearestClamp);
			Vulkan::CommandBufferUtil::draw_quad(cmd,
			                                     "assets://shaders/quad.vert",
			                                     "assets://shaders/post/bloom_downsample.frag",
			                                     {{"FEEDBACK", 1}});
		}
		else
		{
			vec2 inv_size = vec2(1.0f / input.get_image().get_create_info().width,
			                     1.0f / input.get_image().get_create_info().height);
			cmd.push_constants(&inv_size, 0, sizeof(inv_size));
			Vulkan::CommandBufferUtil::draw_quad(cmd,
			                                     "assets://shaders/quad.vert",
			                                     "assets://shaders/post/bloom_downsample.frag");
		}
	}
	else
	{
		vec2 inv_size = vec2(1.0f / input.get_image().get_create_info().width,
		                     1.0f / input.get_image().get_create_info().height);
		cmd.push_constants(&inv_size, 0, sizeof(inv_size));
		Vulkan::CommandBufferUtil::draw_quad(cmd,
		                                     "assets://shaders/quad.vert",
		                                     "assets://shaders/post/bloom_downsample.frag");
	}
}

static void bloom_upsample_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd)
{
	auto &input = pass.get_graph().get_physical_texture_resource(pass.get_texture_inputs()[0]->get_physical_index());
	vec2 inv_size = vec2(1.0f / input.get_image().get_create_info().width, 1.0f / input.get_image().get_create_info().height);
	cmd.push_constants(&inv_size, 0, sizeof(inv_size));
	cmd.set_texture(0, 0, input, Vulkan::StockSampler::LinearClamp);
	Vulkan::CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/post/bloom_upsample.frag");
}

static void tonemap_build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd)
{
	auto &hdr = pass.get_graph().get_physical_texture_resource(pass.get_texture_inputs()[0]->get_physical_index());
	auto &bloom = pass.get_graph().get_physical_texture_resource(pass.get_texture_inputs()[1]->get_physical_index());
	auto &ubo = pass.get_graph().get_physical_buffer_resource(pass.get_uniform_inputs()[0]->get_physical_index());
	cmd.set_texture(0, 0, hdr, Vulkan::StockSampler::LinearClamp);
	cmd.set_texture(0, 1, bloom, Vulkan::StockSampler::LinearClamp);
	cmd.set_uniform_buffer(0, 2, ubo);
	Vulkan::CommandBufferUtil::draw_quad(cmd, "assets://shaders/quad.vert", "assets://shaders/post/tonemap.frag");
}

void setup_hdr_postprocess(RenderGraph &graph, const std::string &input, const std::string &output)
{
	BufferInfo buffer_info;
	buffer_info.size = 3 * sizeof(float);
	buffer_info.persistent = true;
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	auto &lum = graph.get_buffer_resource("average-luminance");
	lum.set_buffer_info(buffer_info);

	auto &adapt_pass = graph.add_pass("adapt-luminance", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	adapt_pass.add_storage_output("average-luminance-updated", buffer_info, "average-luminance");
	adapt_pass.add_texture_input("bloom-downsample-3");
	adapt_pass.set_build_render_pass([&adapt_pass](Vulkan::CommandBuffer &cmd) {
		luminance_build_render_pass(adapt_pass, cmd);
	});

	auto &threshold = graph.add_pass("bloom-threshold", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	AttachmentInfo threshold_info;
	threshold_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	threshold_info.size_x = 0.5f;
	threshold_info.size_y = 0.5f;
	threshold_info.size_class = SizeClass::InputRelative;
	threshold_info.size_relative_name = input;
	threshold.add_color_output("threshold", threshold_info);
	threshold.add_texture_input(input);
	threshold.add_uniform_input("average-luminance");
	threshold.set_build_render_pass([&threshold](Vulkan::CommandBuffer &cmd) {
		bloom_threshold_build_render_pass(threshold, cmd);
	});

	AttachmentInfo blur_info;
	blur_info.size_x = 0.25f;
	blur_info.size_y = 0.25f;
	blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	blur_info.size_class = SizeClass::InputRelative;
	blur_info.size_relative_name = input;
	auto &blur0 = graph.add_pass("bloom-downsample-0", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	blur0.add_color_output("bloom-downsample-0", blur_info);
	blur0.add_texture_input("threshold");
	blur0.set_build_render_pass([&blur0](Vulkan::CommandBuffer &cmd) {
		bloom_downsample_build_render_pass(blur0, cmd, false);
	});

	blur_info.size_x = 0.125f;
	blur_info.size_y = 0.125f;
	blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	auto &blur1 = graph.add_pass("bloom-downsample-1", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	blur1.add_color_output("bloom-downsample-1", blur_info);
	blur1.add_texture_input("bloom-downsample-0");
	blur1.set_build_render_pass([&blur1](Vulkan::CommandBuffer &cmd) {
		bloom_downsample_build_render_pass(blur1, cmd, false);
	});

	blur_info.size_x = 0.0625f;
	blur_info.size_y = 0.0625f;
	blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	auto &blur2 = graph.add_pass("bloom-downsample-2", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	blur2.add_color_output("bloom-downsample-2", blur_info);
	blur2.add_texture_input("bloom-downsample-1");
	blur2.set_build_render_pass([&blur2](Vulkan::CommandBuffer &cmd) {
		bloom_downsample_build_render_pass(blur2, cmd, false);
	});

	blur_info.size_x = 0.03125f;
	blur_info.size_y = 0.03125f;
	blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	auto &blur3 = graph.add_pass("bloom-downsample-3", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	blur3.add_color_output("bloom-downsample-3", blur_info);
	blur3.add_texture_input("bloom-downsample-2");
	blur3.add_history_input("bloom-downsample-3");
	blur3.set_build_render_pass([&blur3](Vulkan::CommandBuffer &cmd) {
		bloom_downsample_build_render_pass(blur3, cmd, true);
	});

	blur_info.size_x = 0.0625f;
	blur_info.size_y = 0.0625f;
	blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	auto &blur4 = graph.add_pass("bloom-upsample-0", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	blur4.add_color_output("bloom-upsample-0", blur_info);
	blur4.add_texture_input("bloom-downsample-3");
	blur4.set_build_render_pass([&blur4](Vulkan::CommandBuffer &cmd) {
		bloom_upsample_build_render_pass(blur4, cmd);
	});

	blur_info.size_x = 0.125f;
	blur_info.size_y = 0.125f;
	blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	auto &blur5 = graph.add_pass("bloom-upsample-1", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	blur5.add_color_output("bloom-upsample-1", blur_info);
	blur5.add_texture_input("bloom-upsample-0");
	blur5.set_build_render_pass([&blur5](Vulkan::CommandBuffer &cmd) {
		bloom_upsample_build_render_pass(blur5, cmd);
	});

	blur_info.size_x = 0.25f;
	blur_info.size_y = 0.25f;
	blur_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	auto &blur6 = graph.add_pass("bloom-upsample-2", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	blur6.add_color_output("bloom-upsample-2", blur_info);
	blur6.add_texture_input("bloom-upsample-1");
	blur6.set_build_render_pass([&blur6](Vulkan::CommandBuffer &cmd) {
		bloom_upsample_build_render_pass(blur6, cmd);
	});

	AttachmentInfo tonemap_info;
	tonemap_info.size_class = SizeClass::InputRelative;
	tonemap_info.size_relative_name = input;
	auto &tonemap = graph.add_pass("tonemap", VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
	tonemap.add_color_output(output, tonemap_info);
	tonemap.add_texture_input(input);
	tonemap.add_texture_input("bloom-upsample-2");
	tonemap.add_uniform_input("average-luminance-updated");
	tonemap.set_build_render_pass([&tonemap](Vulkan::CommandBuffer &cmd) {
		tonemap_build_render_pass(tonemap, cmd);
	});
}
}