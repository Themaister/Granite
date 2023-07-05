/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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
		get_wsi().set_present_mode(Vulkan::PresentMode::UnlockedMaybeTear);
		EVENT_MANAGER_REGISTER_LATCH(BasicComputeTest, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	void on_device_create(const DeviceCreatedEvent &)
	{
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer(CommandBuffer::Type::AsyncCompute);

		struct ReadbackData
		{
			uvec4 local_invocation_ids[1024];
			uint subgroup_ids[1024];
			uint subgroup_invocation_ids[1024];
		};

		uint32_t local_size_x = 64;
		uint32_t local_size_y = 2;
		uint32_t local_size_z = 2;
		uint32_t wave_size = 32;

		Vulkan::BufferCreateInfo buf_info = {};
		buf_info.size = sizeof(ReadbackData);
		buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		buf_info.domain = Vulkan::BufferDomain::CachedHost;
		auto output_buffer = device.create_buffer(buf_info);

		cmd->set_program("assets://shaders/local_size_id_test.comp");
		cmd->set_storage_buffer(0, 0, *output_buffer);
		cmd->set_specialization_constant_mask(0x7);
		cmd->set_specialization_constant(0, local_size_x);
		cmd->set_specialization_constant(1, local_size_y);
		cmd->set_specialization_constant(2, local_size_z);
		cmd->enable_subgroup_size_control(true);
		cmd->set_subgroup_size_log2(true, 5, 5);
		cmd->dispatch(1, 1, 1);
		device.submit(cmd);
		device.wait_idle();

		auto *ptr = static_cast<const ReadbackData *>(device.map_host_buffer(*output_buffer, Vulkan::MEMORY_ACCESS_READ_BIT));

		for (unsigned i = 0; i < local_size_x * local_size_y * local_size_z; i++)
		{
			auto invocation_id = ptr->local_invocation_ids[i].xyz();
			auto subgroup_id = ptr->subgroup_ids[i];
			auto subgroup_invocation_id = ptr->subgroup_invocation_ids[i];

			uvec3 expected_local_invocation{
				i % local_size_x,
				(i / local_size_x) % local_size_y,
				i / (local_size_x * local_size_y)
			};

			uint32_t expected_subgroup_id = i / wave_size;
			uint32_t expected_subgroup_invocation_id = i % wave_size;

			if (!all(equal(invocation_id, expected_local_invocation)))
				LOGE("Wrong invocation ID.\n");
			if (subgroup_id != expected_subgroup_id)
				LOGE("Wrong subgroup ID\n");
			if (subgroup_invocation_id != expected_subgroup_invocation_id)
				LOGE("Wrong subgroup invocation ID.\n");
		}
		LOGI("Done!\n");
		request_shutdown();

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