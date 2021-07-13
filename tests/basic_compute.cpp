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

		cmd->barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
		             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT);

		mat3 inputs[128];
		for (unsigned i = 0; i < 9 * 128; i++)
			*(&inputs[0][0][0] + i) = float(i) + 1.0f;
		auto inputs_buffer = create_ssbo(inputs, sizeof(inputs));
		auto outputs_buffer = create_ssbo(nullptr, sizeof(vec3) * 3);

		cmd->set_program("assets://shaders/mat3_reduce.comp");
		cmd->set_storage_buffer(0, 0, *inputs_buffer);
		cmd->set_storage_buffer(0, 1, *outputs_buffer);
		//*cmd->allocate_typed_constant_data<uvec2>(0, 2, 1) = uvec2(0, 2);
		cmd->dispatch(1, 1, 1);
		device.submit(cmd);

		vec3 outputs[3];
		readback_ssbo(outputs, sizeof(outputs), *outputs_buffer);

#define STEP(S) \
		for (unsigned i = 0; i < S; i++) \
		{ \
			inputs[i][0] += inputs[i + S][0]; \
			inputs[i][1] += inputs[i + S][1]; \
			inputs[i][2] += inputs[i + S][2]; \
		}
		STEP(64);
		STEP(32);
		STEP(16);
		STEP(8);
		STEP(4);
		STEP(2);

		const vec3 reference[3] = {
			inputs[0][2],
			inputs[0][1],
			inputs[1][0],
		};

		for (unsigned i = 0; i < 3; i++)
		{
			LOGI("[%u] = (%f, %f, %f), expected (%f, %f, %f)\n", i,
			     outputs[i][0], outputs[i][1], outputs[i][2],
			     reference[i][0], reference[i][1], reference[i][2]);
			bool is_equal = all(equal(outputs[i], reference[i]));
			if (is_equal)
				LOGI("All good\n");
			else
				LOGE(":(\n");
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