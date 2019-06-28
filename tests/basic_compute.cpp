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
#include "muglm/muglm_impl.hpp"

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

		auto cmd = get_wsi().get_device().request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		cmd->copy_buffer(*buffer, src);
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

		Fence fence;
		Semaphore sem[2];
		get_wsi().get_device().submit(cmd, &fence, 2, sem);
		get_wsi().get_device().add_wait_semaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true);
		fence->wait();

		auto *mapped = get_wsi().get_device().map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
		memcpy(data, mapped, size);
		get_wsi().get_device().unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer(CommandBuffer::Type::AsyncCompute);

		u16vec4 a[3] = {};
		a[0] = floatToHalf(vec4(5000.0f));
		a[1] = floatToHalf(vec4(10000.0f));
		auto buffer_a = create_ssbo(a, sizeof(a));

		cmd->set_program("assets://shaders/compute_add.comp");
		cmd->set_storage_buffer(0, 0, *buffer_a);
		cmd->dispatch(1, 1, 1);

		Semaphore sem[2];
		device.submit(cmd, nullptr, 2, sem);
		device.add_wait_semaphore(CommandBuffer::Type::AsyncTransfer, sem[0], VK_PIPELINE_STAGE_TRANSFER_BIT, true);
		device.add_wait_semaphore(CommandBuffer::Type::Generic, sem[1], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, true);

		readback_ssbo(a, sizeof(a), *buffer_a);
		LOGI("dot_result = %f\n", halfToFloat(a[2].x));
		LOGI("length_a_result = %f\n", halfToFloat(a[2].y));
		LOGI("length_b_result = %f\n", halfToFloat(a[2].z));
		LOGI("distance_result = %f\n", halfToFloat(a[2].w));

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
}