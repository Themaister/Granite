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

struct SubgroupApplication : Granite::Application, Granite::EventHandler
{
	SubgroupApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(SubgroupApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		BufferCreateInfo info = {};
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		info.domain = BufferDomain::Device;
		info.size = 64 * sizeof(vec4);
		test_buffer = e.get_device().create_buffer(info, nullptr);

		info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
		info.size = 64 * sizeof(uint32_t);
		input = e.get_device().create_buffer(info, nullptr);

		BufferViewCreateInfo view = {};
		view.offset = 0;
		view.range = 64 * sizeof(uint32_t);
		view.buffer = input.get();
		view.format = VK_FORMAT_R32_UINT;
		input_view = e.get_device().create_buffer_view(view);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		test_buffer.reset();
		input_view.reset();
		input.reset();
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();
		cmd->set_buffer_view(0, 0, *input_view);
		cmd->allocate_constant_data(0, 1, 64 * 3 * sizeof(vec4));
		cmd->set_storage_buffer(0, 2, *test_buffer);
		cmd->set_program("assets://shaders/subgroup.comp", {{ "WAVE_UNIFORM", 1 }});
		cmd->dispatch(1, 1, 1);
		cmd->set_program("assets://shaders/subgroup.comp", {{ "WAVE_UNIFORM", 0 }});
		cmd->dispatch(1, 1, 1);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->end_render_pass();
		device.submit(cmd);
	}

	BufferHandle test_buffer;
	BufferHandle input;
	BufferViewHandle input_view;
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
		auto *app = new SubgroupApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}