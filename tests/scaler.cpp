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
#include "scaler.hpp"

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
	VideoScaler scaler;

	void on_swapchain_create(const SwapchainParameterEvent &e)
	{
		auto info = ImageCreateInfo::immutable_2d_image(e.get_width(), e.get_height(), VK_FORMAT_R8G8B8A8_SRGB);
		info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		info.misc = IMAGE_MISC_MUTABLE_SRGB_BIT;
		render_target = e.get_device().create_image(info);
		render_target->set_layout(Layout::General);

		auto *shader = e.get_device().get_shader_manager().register_compute("builtin://shaders/util/scaler.comp");
		scaler.set_program(shader->register_variant({})->get_program());
	}

	void on_swapchain_destroy(const SwapchainParameterEvent &)
	{
		render_target.reset();
	}

	void scale_image(CommandBuffer &cmd)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto asset_id = GRANITE_ASSET_MANAGER()->register_asset(
				*GRANITE_FILESYSTEM(), "/tmp/test.png", AssetClass::ImageColor);
		auto *view = device.get_resource_manager().get_image_view_blocking(asset_id);

		VideoScaler::RescaleInfo info = {};
		info.num_output_planes = 1;
		info.output_planes[0] = &render_target->get_view();
		info.input = view;
		info.input_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		info.output_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

		scaler.rescale(cmd, info);

		cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
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
