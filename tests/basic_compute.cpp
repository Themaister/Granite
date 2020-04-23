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
		if (!data)
			info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;
		return get_wsi().get_device().create_buffer(info, data);
	}

	void readback_ssbo(void *data, size_t size, const Buffer &src)
	{
		BufferCreateInfo info = {};
		info.size = size;
		info.domain = BufferDomain::CachedHost;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		auto buffer = get_wsi().get_device().create_buffer(info);

		auto cmd = get_wsi().get_device().request_command_buffer();
		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		             VK_ACCESS_TRANSFER_READ_BIT);
		cmd->copy_buffer(*buffer, src);
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
		             VK_ACCESS_HOST_READ_BIT);

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
		auto cmd = device.request_command_buffer();

		cmd->barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		             VK_ACCESS_MEMORY_READ_BIT);

		int32_t a[12] = {};
		for (unsigned i = 0; i < 8; i++)
			a[i] = (int(i) - 8) << 16;
		auto input_buffer = create_ssbo(a, sizeof(a));
		auto output_buffer = create_ssbo(nullptr, 12 * sizeof(int16_t));

		cmd->set_program("assets://shaders/minmax16.comp");
		cmd->set_storage_buffer(0, 0, *input_buffer);
		cmd->set_storage_buffer(0, 1, *output_buffer);

		auto *v = cmd->allocate_typed_constant_data<ivec4>(1, 0, 1);
		*v = ivec4(10, 10, 10, 10);

		cmd->dispatch(1, 1, 1);
		device.submit(cmd);

		int16_t readback_data[12];
		readback_ssbo(readback_data, sizeof(readback_data), *output_buffer);

		for (unsigned i = 0; i < 4; i++)
		{
			LOGI("min s16 (%d, %d) -> %d, (ref: %d).\n", a[i + 0] >> 16, 20, readback_data[i], std::min(a[i + 0] >> 16, 20));
			LOGI("max s16 (%d, %d) -> %d, (ref: %d).\n", a[i + 4] >> 16, 20, readback_data[i + 4], std::max(a[i + 4] >> 16, 20));
		}

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
} // namespace Granite