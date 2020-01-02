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

struct LinearImageTest : Granite::Application, Granite::EventHandler
{
	LinearImageTest()
	{
		EVENT_MANAGER_REGISTER_LATCH(LinearImageTest, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		LinearHostImageCreateInfo info;
		info.width = 4;
		info.height = 4;
		info.flags = LINEAR_HOST_IMAGE_REQUIRE_LINEAR_FILTER_BIT |
		             LINEAR_HOST_IMAGE_HOST_CACHED_BIT |
		             LINEAR_HOST_IMAGE_IGNORE_DEVICE_LOCAL_BIT;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		info.format = VK_FORMAT_R8G8B8A8_SRGB;
		info.stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		linear = e.get_device().create_linear_host_image(info);

		static const uint32_t odd[] = {
			~0u, 0u, ~0u, 0u,
		};

		static const uint32_t even[] = {
			0u, ~0u, 0u, ~0u,
		};

		auto *mapped = static_cast<uint8_t *>(e.get_device().map_linear_host_image(*linear, MEMORY_ACCESS_WRITE_BIT));
		mapped += linear->get_offset();
		for (unsigned y = 0; y < 4; y++)
		{
			if (y & 1)
				memcpy(mapped, odd, sizeof(odd));
			else
				memcpy(mapped, even, sizeof(even));
			mapped += linear->get_row_pitch_bytes();
		}
		e.get_device().unmap_linear_host_image_and_sync(*linear, MEMORY_ACCESS_WRITE_BIT);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		linear.reset();
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
		cmd->set_texture(0, 0, linear->get_view(), StockSampler::LinearClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		cmd->end_render_pass();
		device.submit(cmd);
	}

	LinearHostImageHandle linear;
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
		auto *app = new LinearImageTest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
