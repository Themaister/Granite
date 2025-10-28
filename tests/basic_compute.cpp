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
#include "muglm/muglm_impl.hpp"
#include "os_filesystem.hpp"
#include <cmath>

using namespace Granite;
using namespace Vulkan;

static constexpr uint32_t Width = 4096;
static constexpr uint32_t Height = 2304;

struct BasicComputeTest : Granite::Application, Granite::EventHandler
{
	BasicComputeTest()
	{
		EVENT_MANAGER_REGISTER_LATCH(BasicComputeTest, on_device_create, on_device_destroy, DeviceCreatedEvent);
		get_wsi().set_present_mode(PresentMode::UnlockedMaybeTear);
	}

	ImageHandle dst, src;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		auto info = ImageCreateInfo::immutable_2d_image(Width, Height, VK_FORMAT_D32_SFLOAT_S8_UINT);
		info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		info.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		dst = e.get_device().create_image(info);
		info.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		src = e.get_device().create_image(info);

		auto cmd = e.get_device().request_command_buffer();
		auto *init_depth = static_cast<float *>(cmd->update_image(*src, {}, { Width, Height, 1 }, 0, 0, { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 }));
		for (unsigned i = 0; i < Width * Height; i++)
			init_depth[i] = 0.5f + 0.1f * std::sin(float(i));
		auto *init_stencil = static_cast<uint8_t *>(cmd->update_image(*src, {}, { Width, Height, 1 }, 0, 0, { VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1 }));
		for (unsigned i = 0; i < Width * Height; i++)
			init_stencil[i] = uint8_t(i * 3);

		cmd->image_barrier(*src, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

		e.get_device().submit(cmd);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		dst.reset();
		src.reset();
	}

	unsigned frames = 0;

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();
		frames++;
		//if (frames >= 1000)
		//	request_shutdown();

		auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_2_COPY_BIT);
		cmd->copy_image(*dst, *src);
		auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_2_COPY_BIT);
		cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
		device.submit(cmd);

		cmd = device.request_command_buffer();
		auto start_slow_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_2_COPY_BIT);
		cmd->copy_image(*dst, *src, {}, {}, { Width, Height, 1 },
						{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 }, { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 });
		cmd->copy_image(*dst, *src, {}, {}, { Width, Height, 1 },
		                { VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1 }, { VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1 });
		auto end_slow_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_2_COPY_BIT);
		cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
		device.submit(cmd);

		device.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), "Copy Fused");
		device.register_time_interval("GPU", std::move(start_slow_ts), std::move(end_slow_ts), "Copy Split");

		cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
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
		auto *app = new BasicComputeTest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
} // namespace Granite