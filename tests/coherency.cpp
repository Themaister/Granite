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
#include "muglm/muglm_impl.hpp"
#include "os_filesystem.hpp"

using namespace Granite;
using namespace Vulkan;

struct CoherencyTest : Granite::Application, Granite::EventHandler
{
	CoherencyTest()
	{
		EVENT_MANAGER_REGISTER_LATCH(CoherencyTest, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	BufferHandle buffer;
	BufferHandle copied_buffer;
	unsigned offset = 0;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		BufferCreateInfo info = {};
		info.size = 4 * 1024;
		info.domain = BufferDomain::CachedHost;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		buffer = e.get_device().create_buffer(info);
		// Ensure base offset is not zero.
		buffer = e.get_device().create_buffer(info);

		info.domain = BufferDomain::Device;
		copied_buffer = e.get_device().create_buffer(info);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		buffer.reset();
		copied_buffer.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		cmd->copy_buffer(*copied_buffer, *buffer);
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
		{
			auto *host_ptr = static_cast<u8vec4 *>(device.map_host_buffer(*buffer, MEMORY_ACCESS_WRITE_BIT));
			host_ptr[0] = u8vec4(0xff, 0, 0, 0);
			host_ptr[1] = u8vec4(0xff, 0, 0, 0);
			host_ptr[2] = u8vec4(0xff, 0, 0, 0);
			device.unmap_host_buffer(*buffer, MEMORY_ACCESS_WRITE_BIT, offset, 4);
		}
		Fence fence;
		device.submit(cmd, &fence);
		fence->wait();

		cmd = device.request_command_buffer();

		cmd->copy_buffer(*copied_buffer, *buffer);
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
		{
			auto *host_ptr = static_cast<u8vec4 *>(device.map_host_buffer(*buffer, MEMORY_ACCESS_WRITE_BIT));
			host_ptr[0] = u8vec4(0, 0xff, 0, 0);
			host_ptr[1] = u8vec4(0, 0xff, 0, 0);
			host_ptr[2] = u8vec4(0, 0xff, 0, 0);
			device.unmap_host_buffer(*buffer, MEMORY_ACCESS_WRITE_BIT, offset, 4);
		}

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->set_opaque_state();
		cmd->set_program("assets://shaders/triangle.vert", "assets://shaders/triangle.frag");

		auto *pos = static_cast<vec2 *>(cmd->allocate_vertex_data(0, sizeof(vec2) * 3, sizeof(vec2)));
		cmd->set_vertex_binding(1, *copied_buffer, 0, sizeof(u8vec4));

		pos[0] = vec2(-1.0f, -1.0f);
		pos[1] = vec2(-1.0f, +3.0f);
		pos[2] = vec2(+3.0f, -1.0f);

		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
		cmd->set_vertex_attrib(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 0);
		cmd->draw(3);

		cmd->end_render_pass();
		fence.reset();
		device.submit(cmd, &fence);
		fence->wait();
	}
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
		auto *app = new CoherencyTest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
} // namespace Granite