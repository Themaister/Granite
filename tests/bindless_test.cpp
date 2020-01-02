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

#include "application.hpp"
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "math.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct BindlessApplication : Granite::Application, Granite::EventHandler
{
	BindlessApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(BindlessApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		ImageCreateInfo info = ImageCreateInfo::immutable_2d_image(1, 1, VK_FORMAT_R8G8B8A8_SRGB);
		ImageInitialData data = {};

		const uint8_t red[] = { 0xff, 0, 0, 0xff };
		const uint8_t green[] = { 0, 0xff, 0, 0xff };
		const uint8_t blue[] = { 0, 0, 0xff, 0xff };
		const uint8_t black[] = { 0, 0, 0, 0xff };
		data.data = red;
		images[0] = e.get_device().create_image(info, &data);
		data.data = green;
		images[1] = e.get_device().create_image(info, &data);
		data.data = blue;
		images[2] = e.get_device().create_image(info, &data);
		data.data = black;
		images[3] = e.get_device().create_image(info, &data);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		for (auto &image : images)
			image.reset();
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		cmd->begin_render_pass(rp);

		auto bindless = device.create_bindless_descriptor_pool(BindlessResourceType::ImageFP, 1, 1024);
		bindless->allocate_descriptors(1024);
		for (unsigned i = 0; i < 1024; i++)
			bindless->set_texture(i, images[i & 3]->get_view());
		cmd->set_bindless(0, bindless->get_descriptor_set());
		cmd->set_bindless(2, bindless->get_descriptor_set());
		cmd->set_sampler(1, 2, StockSampler::LinearClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/bindless.frag");

		cmd->end_render_pass();
		device.submit(cmd);
	}

	ImageHandle images[4];
};

namespace Granite
{
Application *application_create(int, char **)
{
	application_dummy();

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	try
	{
		auto *app = new BindlessApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
