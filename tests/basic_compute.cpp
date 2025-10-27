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

using namespace Granite;
using namespace Vulkan;

static constexpr uint32_t Width = 1024;
static constexpr uint32_t Height = 576;
static constexpr uint32_t Depth = 64;

#if 1
static constexpr VkImageUsageFlags ImageUsage =
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
static constexpr VkImageCreateFlags ImageCreate = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
#else
static constexpr VkImageUsageFlags ImageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
static constexpr VkImageCreateFlags ImageCreate = 0;
#endif

struct BasicComputeTest : Granite::Application, Granite::EventHandler
{
	BasicComputeTest()
	{
		EVENT_MANAGER_REGISTER_LATCH(BasicComputeTest, on_device_create, on_device_destroy, DeviceCreatedEvent);
		get_wsi().set_present_mode(PresentMode::UnlockedMaybeTear);
	}

	ImageHandle img;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		auto info = ImageCreateInfo::immutable_3d_image(Width, Height, Depth, VK_FORMAT_R8G8B8A8_UNORM);
		info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		info.usage = ImageUsage;
		info.flags = ImageCreate;
		img = e.get_device().create_image(info);
		img->set_layout(Layout::General);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		img.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		struct Config {
			const char *tag;
			uint32_t wg_size[3];
			bool rmw;
		};

		static const Config configs[] = {
			{ "8x8x1 write-only", { 8, 8, 1 }, false },
			{ "8x8x1 read-write", { 8, 8, 1 }, true },
			{ "16x16x1 write-only", { 16, 16, 1 }, false },
			{ "16x16x1 read-write", { 16, 16, 1 }, true },
			{ "4x4x4 write-only", { 4, 4, 4 }, false },
			{ "4x4x4 read-write", { 4, 4, 4 }, true },
			{ "8x8x4 write-only", { 8, 8, 4 }, false },
			{ "8x8x4 read-write", { 8, 8, 4 }, true },
			{ "8x8x8 write-only", { 8, 8, 8 }, false },
			{ "8x8x8 read-write", { 8, 8, 8 }, true },
		};

		cmd->set_program("assets://shaders/image-3d.comp");
		cmd->set_storage_texture(0, 0, img->get_view());

		for (auto &config : configs)
		{
			cmd->set_specialization_constant_mask(0xf);
			for (int i = 0; i < 3; i++)
				cmd->set_specialization_constant(i, config.wg_size[i]);
			cmd->set_specialization_constant(3, config.rmw);

			auto start_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			cmd->dispatch(img->get_width() / config.wg_size[0], img->get_height() / config.wg_size[1], img->get_depth() / config.wg_size[2]);
			auto end_ts = cmd->write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			cmd->barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
			device.register_time_interval("GPU", std::move(start_ts), std::move(end_ts), config.tag);
		}
		device.submit(cmd);

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