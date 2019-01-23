/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

using namespace Granite;
using namespace Vulkan;

struct BasicComputeTest : Granite::Application, Granite::EventHandler
{
	BasicComputeTest()
	{
		EVENT_MANAGER_REGISTER_LATCH(BasicComputeTest, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	void on_device_create(const DeviceCreatedEvent &)
	{
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
	}

	BufferHandle create_ssbo(const void *data, size_t size)
	{
		BufferCreateInfo info = {};
		info.size = size;
		info.domain = BufferDomain::Device;
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		return get_wsi().get_device().create_buffer(info, data);
	}

	void readback_ssbo(void *data, size_t size, const Buffer &src)
	{
		BufferCreateInfo info = {};
		info.size = size;
		info.domain = BufferDomain::CachedHost;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		auto buffer = get_wsi().get_device().create_buffer(info);

		auto cmd = get_wsi().get_device().request_command_buffer(CommandBuffer::Type::Generic);
		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		cmd->copy_buffer(*buffer, src);
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

		Fence fence;
		get_wsi().get_device().submit(cmd, &fence);
		fence->wait();

		auto *mapped = get_wsi().get_device().map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
		memcpy(data, mapped, size);
		get_wsi().get_device().unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer(CommandBuffer::Type::Generic);

		float a[64];
		float b[64];
		for (auto &v : a)
			v = 1.0f;
		for (auto &v : b)
			v = 3.0f;

		auto buffer_a = create_ssbo(a, sizeof(a));
		auto buffer_b = create_ssbo(b, sizeof(b));
		auto buffer_c = create_ssbo(nullptr, sizeof(a));

		cmd->set_program("assets://shaders/compute_add.comp");
		cmd->set_storage_buffer(0, 0, *buffer_a);
		cmd->set_storage_buffer(0, 1, *buffer_b);
		cmd->set_storage_buffer(0, 2, *buffer_c);
		cmd->dispatch(1, 1, 1);
		device.submit(cmd);

		float c[64];
		readback_ssbo(c, sizeof(c), *buffer_c);
		for (auto &v : c)
			LOGI("c[] = %f\n", v);
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
		auto *app = new BasicComputeTest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}