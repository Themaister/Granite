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

#include "application.hpp"
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct ScalerApplication : Granite::Application, Granite::EventHandler
{
	ScalerApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(ScalerApplication, on_swapchain_create, on_swapchain_destroy, SwapchainParameterEvent);
		get_wsi().set_present_mode(PresentMode::UnlockedMaybeTear);
	}

	ImageHandle render_target;

	void on_swapchain_create(const SwapchainParameterEvent &e)
	{
		auto info = ImageCreateInfo::immutable_2d_image(e.get_width(), e.get_height(), VK_FORMAT_R16G16B16A16_SFLOAT);
		info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		render_target = e.get_device().create_image(info);
		render_target->set_layout(Layout::General);
	}

	void on_swapchain_destroy(const SwapchainParameterEvent &)
	{
		render_target.reset();
	}

	static float sinc(float v)
	{
		v *= muglm::pi<float>();
		if (muglm::abs(v) < 0.0001f)
			return 1.0f;
		else
			return muglm::sin(v) / v;
	}

	void scale_image(CommandBuffer &cmd)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto asset_id = GRANITE_ASSET_MANAGER()->register_asset(
				*GRANITE_FILESYSTEM(), "/tmp/test.jpg", AssetClass::ImageColor);
		auto *view = device.get_resource_manager().get_image_view_blocking(asset_id);

		constexpr int Phases = 256;
		constexpr int Taps = 8;

		struct Push
		{
			ivec2 resolution;
			vec2 scaling_to_input;
		} push = {};

		push.resolution.x = int(view->get_view_width());
		push.resolution.y = int(view->get_view_height());
		push.scaling_to_input.x = float(push.resolution.x) / float(device.get_swapchain_view().get_view_width());
		push.scaling_to_input.y = float(push.resolution.y) / float(device.get_swapchain_view().get_view_height());
		cmd.push_constants(&push, 0, sizeof(push));

		cmd.set_specialization_constant_mask(1);
		cmd.set_specialization_constant(0, push.scaling_to_input.x > 1.0f || push.scaling_to_input.y > 1.0f);
		cmd.enable_subgroup_size_control(true);
		cmd.set_subgroup_size_log2(true, 2, 6);

		float bw = 1.0f / push.scaling_to_input.x * 0.8f;
		float bh = 1.0f / push.scaling_to_input.y * 0.8f;

		float weights_data[2][Phases][Taps] = {};

		for (int phase = 0; phase < Phases; phase++)
		{
			float total_horiz = 0.0f;
			float total_vert = 0.0f;

			for (int tap = 0; tap < Taps; tap++)
			{
				constexpr int HalfTaps = Taps / 2;
				constexpr int TapOffset = HalfTaps - 1;
				float l = float(tap - TapOffset) - float(phase) / float(Phases);

				float w_horiz = sinc(l / float(HalfTaps)) * sinc(bw * l);
				float w_vert = sinc(l / float(HalfTaps)) * sinc(bh * l);

				total_horiz += w_horiz;
				total_vert += w_vert;

				weights_data[0][phase][tap] = w_horiz;
				weights_data[1][phase][tap] = w_vert;
			}

			for (auto &w : weights_data[0][phase])
				w /= total_horiz;
			for (auto &w : weights_data[1][phase])
				w /= total_vert;
		}

		BufferCreateInfo weights_info = {};
		weights_info.size = Phases * Taps * sizeof(float) * 2;
		weights_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		weights_info.domain = BufferDomain::Device;

		auto weights = device.create_buffer(weights_info, weights_data);

		cmd.set_program("builtin://shaders/util/scaler.comp");
		cmd.set_texture(0, 0, *view);
		cmd.set_storage_texture(0, 1, render_target->get_view());
		cmd.set_storage_buffer(0, 4, *weights);

		auto start_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		cmd.dispatch((render_target->get_width() + 7) / 8, (render_target->get_height() + 7) / 8, 1);
		auto end_ts = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		device.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "scale");
	}

	void render_frame(double, double) override
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		scale_image(*cmd);
		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->set_texture(0, 0, render_target->get_view(), StockSampler::NearestClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		cmd->end_render_pass();
		device.submit(cmd);
	}
};

namespace Granite
{
Application *application_create(int, char **)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new ScalerApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
