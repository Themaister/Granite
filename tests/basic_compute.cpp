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

	void readback_image(void *data, size_t size, const Image &src)
	{
		BufferCreateInfo info = {};
		info.size = size;
		info.domain = BufferDomain::CachedHost;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		auto buffer = get_wsi().get_device().create_buffer(info);

		auto cmd = get_wsi().get_device().request_command_buffer(CommandBuffer::Type::AsyncTransfer);
		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		cmd->copy_image_to_buffer(*buffer, src, 0, {}, { src.get_width(), src.get_height(), src.get_depth() }, 0, 0,
								  { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

		Fence fence;
		Semaphore sem;
		get_wsi().get_device().submit(cmd, &fence, 1, &sem);
		fence->wait();

		get_wsi().get_device().add_wait_semaphore(CommandBuffer::Type::AsyncCompute, sem, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true);

		auto *mapped = get_wsi().get_device().map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
		memcpy(data, mapped, size);
		get_wsi().get_device().unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
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
		Semaphore sem;
		get_wsi().get_device().submit(cmd, &fence, 1, &sem);
		fence->wait();

		get_wsi().get_device().add_wait_semaphore(CommandBuffer::Type::AsyncCompute, sem, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true);

		auto *mapped = get_wsi().get_device().map_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
		memcpy(data, mapped, size);
		get_wsi().get_device().unmap_host_buffer(*buffer, MEMORY_ACCESS_READ_BIT);
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer(CommandBuffer::Type::AsyncCompute);

		float a[64];
		float b[64];
		for (auto &v : a)
			v = 1.0f;
		for (auto &v : b)
			v = 3.0f;

		auto buffer_a = create_ssbo(a, sizeof(a));
		auto buffer_b = create_ssbo(b, sizeof(b));
		auto buffer_c = create_ssbo(nullptr, sizeof(a));

		ImageHandle img;
		{
			ImageCreateInfo info = ImageCreateInfo::immutable_2d_image(8, 4, VK_FORMAT_R16G16B16A16_SFLOAT);
			info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
			img = device.create_image(info);
			img->set_layout(Layout::General);
		}

		cmd->set_program("assets://shaders/compute_add.comp");
		cmd->set_storage_buffer(1, 0, *buffer_a);
		cmd->set_storage_buffer(1, 1, *buffer_b);
		cmd->set_storage_buffer(1, 2, *buffer_c);
		cmd->set_storage_texture(0, 1, img->get_view());
		*cmd->allocate_typed_constant_data<float>(0, 0, 1) = 2.0f;
		float push = 10.0f;
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->dispatch(1, 1, 1);
		cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
					 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
		push = 0.0f;
		cmd->push_constants(&push, 0, sizeof(push));
		cmd->dispatch(1, 1, 1);

		Semaphore sem;
		device.submit(cmd, nullptr, 1, &sem);
		device.add_wait_semaphore(CommandBuffer::Type::AsyncTransfer, sem, VK_PIPELINE_STAGE_TRANSFER_BIT, true);

		float c[64] = {};
		readback_ssbo(c, sizeof(c), *buffer_c);
		for (auto &v : c)
			LOGI("c[] = %f\n", v);

		u16vec4 cs[64];
		readback_image(cs, sizeof(cs), *img);
		for (auto &v : cs)
			LOGI("cs[] = 0x%x\n", v.x);

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